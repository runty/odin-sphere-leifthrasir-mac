#!/usr/bin/env python3
"""Check snapshot request bounds and PNG pixels with an independent PNG decoder."""
import argparse,hashlib,json,struct,subprocess,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',required=True,type=Path)
a=p.parse_args();root=a.root.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount():p.error('Use mounted external test storage')
root.mkdir(parents=True,exist_ok=True)
source=r'''
#include "osl_frame_snapshot.h"
#include "osl_png.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace OslFrameSnapshot;
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 unsigned checks=0;auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("snapshot check "+std::to_string(checks));};
 auto request=Parse("frame.png",nullptr);require(request && request->frame==1000 && request->path=="frame.png");
 require(request->source==Source::Game);
 require(Parse("display.png","1100","display")->source==Source::Display);
 require(Parse("game.png","1","game")->source==Source::Game);
 for(auto s:{"","DISPLAY","Game"," display","display ","unknown"})require(!Parse("frame.png","1",s));
 for(auto s:{"1","1000","1000000"})require(Parse("frame.png",s).has_value());
 for(auto s:{"","0","-1","+1"," 1","1 ","1x","1.0","1000001","18446744073709551616"})require(!Parse("frame.png",s));
 require(!Parse(nullptr,"1"));require(!Parse("","1"));
 require(Parse(std::string(4096,'a').c_str(),"1").has_value());require(!Parse(std::string(4097,'a').c_str(),"1"));
 require(request->every==0 && request->count==1);
 require(!request->Captures(999) && request->Captures(1000) && !request->Captures(1001));
 require(request->PathAt(1000)=="frame.png");
 require(!Parse("sequence","5","display","3",nullptr));
 require(!Parse("sequence","5","display",nullptr,"4"));
 for(auto s:{"","0","-1","+1"," 1","1 ","1x","1000001","18446744073709551616"})
  require(!Parse("sequence","5","display",s,"4"));
 for(auto s:{"","0","1","65","-2","+2"," 2","2 ","2x","18446744073709551616"})
  require(!Parse("sequence","5","display","3",s));
 require(Parse("sequence","1","display","1","64").has_value());
 require(Parse("sequence","999937","display","1","64").has_value());
 require(!Parse("sequence","999938","display","1","64"));
 require(Parse("sequence","1","display","999999","2").has_value());
 require(!Parse("sequence","1","display","1000000","2"));
 require(!Parse("sequence","1000000","display","1","2"));
 require(Parse(std::string(4064,'a').c_str(),"1","game","1","2").has_value());
 require(!Parse(std::string(4065,'a').c_str(),"1","game","1","2"));
 auto sequence=Parse((out/"sequence").c_str(),"5","display","3","4");
 require(sequence && sequence->frame==5 && sequence->every==3 && sequence->count==4 && sequence->source==Source::Display);
 std::vector<uint64_t> captured;
 for(uint64_t frame=0;frame<25;++frame)if(sequence->Captures(frame))captured.push_back(frame);
 require(captured==std::vector<uint64_t>{5,8,11,14});
 require(!sequence->Captures(UINT64_MAX));
 require(!Request{"invalid",1,Source::Game,0,2}.Captures(1));
 const std::array<unsigned char,24> rgba{255,0,0,255,0,255,0,128,0,0,255,0,0,255,255,42,255,0,255,201,255,255,0,255};
 auto bgra=rgba;for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
 require(OslPng::WriteRgba((out/"rgba.png").c_str(),3,2,rgba.data(),rgba.size()));
 require(OslPng::WriteRgba((out/"bgra.png").c_str(),3,2,bgra.data(),bgra.size(),true));
 for(auto frame:captured) {
  auto pixels=rgba;pixels[0]=static_cast<unsigned char>(frame);
  require(sequence->PathAt(frame)==(out/("sequence.frame-"+std::to_string(frame)+".png")).string());
  require(OslPng::WriteRgba(sequence->PathAt(frame).c_str(),3,2,pixels.data(),pixels.size()));
 }
 auto invalid=out/"invalid.png";{std::ofstream f(invalid);f<<"keep";}
 require(!OslPng::WriteRgba(nullptr,3,2,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba("",3,2,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),0,2,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),3,0,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),4097,2,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),3,4097,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),UINT32_MAX,UINT32_MAX,rgba.data(),rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),3,2,nullptr,rgba.size()));
 require(!OslPng::WriteRgba(invalid.c_str(),3,2,rgba.data(),rgba.size()-1));
 require(!OslPng::WriteRgba(invalid.c_str(),3,2,rgba.data(),rgba.size()+1));
 require(!OslPng::WriteRgba((out/"missing/file.png").c_str(),3,2,rgba.data(),rgba.size()));
 std::ifstream f(invalid);std::string contents{std::istreambuf_iterator<char>(f),{}};require(contents=="keep");
 std::cout<<checks<<" checks passed\n";
}
'''
(root/'check.cpp').write_text(source)
# Decode standard 8-bit RGBA PNGs without using the writer's libpng code.
def decode(path):
 data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';at=8;compressed=bytearray();dimensions=None
 while at<len(data):
  length,=struct.unpack_from('>I',data,at);kind=data[at+4:at+8];body=data[at+8:at+8+length];crc,=struct.unpack_from('>I',data,at+8+length)
  assert crc==zlib.crc32(kind+body)&0xffffffff
  if kind==b'IHDR':
   width,height,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',body)
   assert (depth,color,compression,filtering,interlace)==(8,6,0,0,0);dimensions=width,height
  elif kind==b'IDAT':compressed+=body
  elif kind==b'IEND':assert at+12==len(data)
  at+=12+length
 assert dimensions
 width,height=dimensions;raw=zlib.decompress(compressed);stride=width*4;assert len(raw)==height*(stride+1);rows=[];previous=bytearray(stride)
 def paeth(a,b,c):
  v=a+b-c;da,db,dc=abs(v-a),abs(v-b),abs(v-c)
  return a if da<=db and da<=dc else b if db<=dc else c
 for y in range(height):
  mode=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)]);assert mode<=4
  for x in range(stride):
   left=row[x-4] if x>=4 else 0;above=previous[x];corner=previous[x-4] if x>=4 else 0
   predictor=(0,left,above,(left+above)//2,paeth(left,above,corner))[mode];row[x]=(row[x]+predictor)&255
  rows.append(bytes(row));previous=row
 return dimensions,b''.join(rows)
expected=bytes([255,0,0,255,0,255,0,128,0,0,255,0,0,255,255,42,255,0,255,201,255,255,0,255])
results=[]
for name,flags in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 command=['xcrun','clang++','-arch','arm64','-std=c++23','-O1','-I'+str(repo/'gpu/shim'),'-I/opt/homebrew/include',*flags,str(root/'check.cpp'),str(repo/'gpu/shim/osl_png.cpp'),'-L/opt/homebrew/lib','-lpng','-lz','-o',str(root/name)]
 with (root/(name+'-build.log')).open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
 output=subprocess.check_output([str(root/name),str(root/(name+'-output'))],text=True)
 for format in ('rgba','bgra'):
  dimensions,pixels=decode(root/(name+'-output')/(format+'.png'));assert dimensions==(3,2) and pixels==expected
 for frame in (5,8,11,14):
  dimensions,pixels=decode(root/(name+'-output')/f'sequence.frame-{frame}.png');assert dimensions==(3,2) and pixels==bytes([frame])+expected[1:]
 results.append({'mode':name,'checks':int(output.split()[0]),'independently_decoded_pngs':6,'pixels_exact':True,'sequence_frames':[5,8,11,14]})
report={'passed':True,'scope':'bounded one-shot/sequence request parsing, schedule, unique paths and PNG channels/alpha/row order/bounds; GPU readback is separately tested by real renderer runs','results':results,'sources':{str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [repo/'gpu/shim/osl_png.h',repo/'gpu/shim/osl_png.cpp',repo/'gpu/shim/osl_frame_snapshot.h']}}
(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

#!/usr/bin/env python3
"""Exercise real host protection with old/new renderer tracking headers.

No renderer/game/device execution or cache-coherence acceptance is inferred.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--baseline-source',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2];baseline=a.baseline_source.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount() or out.exists():p.error('Use a new directory on mounted external storage')
out.mkdir(parents=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=[repo/'ios/graphics/page-granularity-test.cpp',Path(__file__)]+[source/name for source in (baseline,repo) for name in ['gpu/shadps4/video_core/buffer_cache/region_definitions.h','gpu/shadps4/video_core/page_manager.h','gpu/shadps4/video_core/page_manager.cpp','gpu/shadps4/common/signal_context.cpp','gpu/shadps4/common/arch.h']]
inputs={str(path):sha(path) for path in paths};results={}
def run(command,label):
 with (out/(label+'.log')).open('w') as f:subprocess.run(list(map(str,command)),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
for label,source in [('baseline',baseline),('fixed',repo)]:
 for arch in ('arm64','x86_64'):
  key=label+'-'+arch;exe=out/key
  run(['xcrun','clang++','-arch',arch,'-std=c++20','-O2','-I'+str(source/'gpu/shadps4'),repo/'ios/graphics/page-granularity-test.cpp',source/'gpu/shadps4/common/signal_context.cpp','-o',exe],key+'-build')
  run([exe],key+'-run');lines=(out/(key+'-run.log')).read_text().splitlines();reports=[json.loads(s[len('RESULT:'):]) for s in lines if s.startswith('RESULT:')];assert len(reports)==1;report=reports[0]
  expected=6 if label=='baseline' and arch=='arm64' else 0
  assert report['cases']==8 and report['unresolved_faults']==expected and report['other_failures']==0
  assert report['tracker_page_bytes']==(16384 if label=='fixed' and arch=='arm64' else 4096)
  assert report['host_page_bytes']==(16384 if arch=='arm64' else 4096)
  results[key]=dict(report,binary_sha256=sha(exe))
for sdk in ('iphoneos','iphonesimulator'):
 sysroot=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip();target='arm64-apple-ios27.2'+('-simulator' if sdk=='iphonesimulator' else '')
 for label,source in [('baseline',baseline),('fixed',repo)]:
  for name,path in [('fixture',repo/'ios/graphics/page-granularity-test.cpp'),('signal-context',source/'gpu/shadps4/common/signal_context.cpp')]:
   run(['xcrun','clang++','-target',target,'-isysroot',sysroot,'-std=c++20','-O2','-I'+str(source/'gpu/shadps4'),'-c',path,'-o',out/(label+'-'+sdk+'-'+name+'.o')],label+'-'+sdk+'-'+name+'-compile')
for name,digest in inputs.items():assert sha(Path(name))==digest
report={'passed':True,'scope':'Actual PageManager header policy and Darwin mprotect/fault recovery only; no renderer cache-coherence, gameplay or physical-device acceptance','native_results':results,'mobile_sdks_compiled':['iphoneos','iphonesimulator'],'sources':inputs,'phone_contacted':False,'device_contacted':False}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:{'unresolved_faults':v['unresolved_faults'],'tracker_page_bytes':v['tracker_page_bytes']} for k,v in results.items()}))

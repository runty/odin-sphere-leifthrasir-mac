#!/usr/bin/env python3
"""Package the exact passing host-fault fixture in an iPad-only diagnostic.

No game assets, game saves, renderer/audio initialization, device calls or
installation are performed by this builder.
"""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--baseline-source',type=Path,required=True)
p.add_argument('--verification',type=Path,required=True)
p.add_argument('--team',required=True)
p.add_argument('--sdk',choices=('iphoneos','iphonesimulator'),default='iphoneos')
a=p.parse_args();root=a.root.resolve();baseline=a.baseline_source.resolve();repo=Path(__file__).resolve().parents[2]
if len(root.parts)<3 or root.parts[1]!='Volumes' or not Path('/Volumes',root.parts[2]).is_mount() or root.exists():p.error('Use a new external directory on a mounted disk')
if not re.fullmatch('[A-Z0-9]{10}',a.team):p.error('Invalid team')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
proof=json.loads(a.verification.read_text());assert proof['passed']
for key in ('baseline-arm64','fixed-arm64'):
 row=proof['native_results'][key];assert row['cases']==8 and row['other_failures']==0 and row['unresolved_faults']==(6 if key.startswith('baseline') else 0)
fixture=repo/'ios/graphics/page-granularity-test.cpp';ui=repo/'ios/graphics/page-probe.mm';paths=[fixture,ui,Path(__file__),a.verification]
for source in (baseline,repo):
 for name in ('gpu/shadps4/video_core/buffer_cache/region_definitions.h','gpu/shadps4/video_core/page_manager.h','gpu/shadps4/common/signal_context.cpp','gpu/shadps4/common/arch.h'):
  path=source/name;paths.append(path)
  # The preserved baseline tree is byte-identical to the original old-policy
  # source used in the host proof, even if its path differs after integration.
  matching=[h for n,h in proof['sources'].items() if n.endswith('/'+name)]
  assert sha(path) in matching,(str(path),'does not match qualified policy inputs')
assert proof['sources'][str(fixture)]==sha(fixture)
inputs={str(path):sha(path) for path in paths};root.mkdir();objects=root/'Objects';objects.mkdir();source=root/'Source';source.mkdir()
def check():
 for name,digest in inputs.items():assert sha(Path(name))==digest,('Source changed',name)
def run(name,command):
 with (root/(name+'.log')).open('w') as log:subprocess.run(list(map(str,command)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
sdk=subprocess.check_output(['xcrun','--sdk',a.sdk,'--show-sdk-path'],text=True).strip();target='arm64-apple-ios27.2'+('-simulator' if a.sdk=='iphonesimulator' else '');common=['-target',target,'-isysroot',sdk,'-std=c++20','-O2'];members=[]
for label,tree in [('baseline',baseline),('fixed',repo)]:
 obj=objects/(label+'.o');members.append(obj);macros=['-Dmain=osl_page_policy_'+label]+(['-DVideoCore=BaselineVideoCore'] if label=='baseline' else [])
 run(label,['xcrun','clang++',*common,*macros,'-I'+str(tree/'gpu/shadps4'),'-c',fixture,'-o',obj])
obj=objects/'signal-context.o';members.append(obj);run('signal-context',['xcrun','clang++',*common,'-I'+str(repo/'gpu/shadps4'),'-c',repo/'gpu/shadps4/common/signal_context.cpp','-o',obj])
run('archive',['xcrun','libtool','-static','-o',source/'libpage-policy.a',*members]);shutil.copy2(ui,source/'main.mm')
policy_inputs_sha=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()
icon=repo/'packaging/icons/app-icon-256.png';inputs[str(icon)]=sha(icon);assets=source/'Assets.xcassets/AppIcon.appiconset';assets.mkdir(parents=True)
run('icon',['sips','-z','1024','1024',icon,'--out',assets/'Icon.png']);(assets/'Contents.json').write_text(json.dumps({'images':[{'filename':'Icon.png','idiom':'universal','platform':'ios','size':'1024x1024'}],'info':{'author':'xcode','version':1}})+'\n');(assets.parent/'Contents.json').write_text(json.dumps({'info':{'author':'xcode','version':1}})+'\n')
project={'name':'OdinPagePolicy','options':{'deploymentTarget':{'iOS':'27.2'}},'targets':{'OdinPagePolicy':{'type':'application','platform':'iOS','sources':[{'path':'Source','excludes':['*.a']}],'dependencies':[{'sdk':'UIKit.framework'},{'framework':'Source/libpage-policy.a','embed':False}],'settings':{'base':{'PRODUCT_BUNDLE_IDENTIFIER':'com.wadscorp.odinsphere.pagepolicy','DEVELOPMENT_TEAM':a.team,'CODE_SIGN_STYLE':'Automatic','ARCHS':'arm64','TARGETED_DEVICE_FAMILY':'2','CLANG_ENABLE_OBJC_ARC':True,'LIBRARY_SEARCH_PATHS':'$(PROJECT_DIR)/Source','CLANG_CXX_LANGUAGE_STANDARD':'c++20','ASSETCATALOG_COMPILER_APPICON_NAME':'AppIcon','CURRENT_PROJECT_VERSION':1,'MARKETING_VERSION':'0.1'}},'info':{'path':'Info.plist','properties':{'CFBundleDisplayName':'Odin Page Protection','UILaunchScreen':{},'UIApplicationSceneManifest':{'UIApplicationSupportsMultipleScenes':False},'UIFileSharingEnabled':True,'LSSupportsOpeningDocumentsInPlace':True,'OSLFixtureSHA':sha(fixture),'OSLPolicyInputsSHA':policy_inputs_sha,'UISupportedInterfaceOrientations':['UIInterfaceOrientationPortrait','UIInterfaceOrientationPortraitUpsideDown','UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight']}}}}}
(root/'project.json').write_text(json.dumps(project,indent=2)+'\n');check();run('generate',['xcodegen','generate','--spec',root/'project.json','--project',root])
run('build',['xcodebuild','-project',root/'OdinPagePolicy.xcodeproj','-scheme','OdinPagePolicy','-configuration','Release','-sdk',a.sdk,'-destination','generic/platform=iOS'+(' Simulator' if a.sdk=='iphonesimulator' else ''),'-derivedDataPath',root/'DerivedData','-allowProvisioningUpdates','build'])
app=root/'DerivedData/Build/Products'/('Release-'+a.sdk)/'OdinPagePolicy.app';run('codesign',['codesign','--verify','--deep','--strict',app]);check()
manifest={'built':True,'app':str(app),'bundle_id':'com.wadscorp.odinsphere.pagepolicy','sdk':a.sdk,'sources':inputs,'policy_inputs_sha256':policy_inputs_sha,'executable_sha256':sha(app/'OdinPagePolicy'),'game_booted':False,'game_assets_included':False,'device_contacted':False};(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'built':True,'app':str(app),'device_contacted':False}))

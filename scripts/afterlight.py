#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Afterlight project creation and export tools (Python 3 standard library)."""
from __future__ import annotations
import argparse, copy, json, os, pathlib, shutil, subprocess, sys, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
def write_json(path, data):
    path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def create_project(destination, name):
    destination=destination.resolve()
    if destination.exists():raise ValueError('Choose a new project directory; existing directories are never overwritten.')
    if not name.strip() or len(name.encode())>=128:raise ValueError('Project name must contain 1–127 UTF-8 bytes.')
    destination.parent.mkdir(parents=True,exist_ok=True)
    stage=pathlib.Path(tempfile.mkdtemp(prefix='.afterlight-new-',dir=destination.parent))
    try:
        (stage/'scenes').mkdir();(stage/'game').mkdir();(stage/'assets').mkdir()
        shutil.copytree(ROOT/'engine/include/afterlight',stage/'include/afterlight')
        shutil.copy2(ROOT/'game/starter_game.c',stage/'game/game.c');shutil.copy2(ROOT/'engine/src/module_sanitizer.c',stage/'game/module_sanitizer.c')
        shutil.copy2(ROOT/'LICENSE',stage/'LICENSE')
        (stage/'tools').mkdir();shutil.copy2(ROOT/'scripts/package_source.py',stage/'tools/package_source.py')
        (stage/'NOTICE').write_text('''Afterlight starter project

This Source Code Form is subject to the terms of the Mozilla Public License,
v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain
one at https://mozilla.org/MPL/2.0/.

Copied Afterlight public headers, game templates, build scripts and tools are
MPL-2.0, including modifications to those files. Original generated scene data
is also MPL-2.0. Independent files may use their own license; this notice does
not apply MPL to independent unmarked game files or imported assets.

Builds create game<module suffix>.source.zip beside the game module, containing
marked MPL source files, build scripts and these notices. Exports include that
archive as source/game-source.zip, alongside the engine source archive and MPL.
Keep notices on covered files, mark new files containing covered code, and
provide corresponding source for all covered modifications when distributing.
''')
        source=json.loads((ROOT/'sandbox/scenes/demo.afterlight.json').read_text());source['assets']=[];source['local_lights']=[]
        floor=copy.deepcopy(next(o for o in source['objects'] if o['name']=='Ground'));floor.update(id='1',name='Ground',position=[0,-.65,0],scale=[16,1,16])
        player=copy.deepcopy(floor);player.update(id='2',name='Player',visible=False,position=[0,.902,4],scale=[1,1,1]);player['collider'].update(type='capsule',center=[0,0,0],radius=.35,half_height=.55)
        player['gameplay']['player']['enabled']=True;player['gameplay']['camera'].update(enabled=True,primary=True)
        source['objects']=[floor,player];source['next_id']='3';write_json(stage/'scenes/start.afterlight.json',source)
        cmake='''# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.24)
project(AfterlightGame LANGUAGES C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_library(game MODULE game/module_sanitizer.c game/game.c)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(game_source
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_source.py"
        --project --root "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/game${CMAKE_SHARED_MODULE_SUFFIX}.source.zip"
    VERBATIM)
add_dependencies(game game_source)
target_include_directories(game PRIVATE include)
set_target_properties(game PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
if(UNIX)
    target_link_libraries(game PRIVATE m)
endif()
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-Xclang -fsanitize-address-destructor=none" HAS_ASAN_DTOR_OPTION)
    if(HAS_ASAN_DTOR_OPTION)
        target_compile_options(game PRIVATE -Xclang -fsanitize-address-destructor=none)
    endif()
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/game.afterlight.project" CONTENT
    "{\\"format\\":\\"afterlight.project\\",\\"version\\":1,\\"name\\":@NAME@,\\"startup_scene\\":\\"../scenes/start.afterlight.json\\",\\"game_module\\":\\"$<TARGET_FILE_NAME:game>\\"}\\n")
'''
        # JSON string inside a CMake quoted argument.
        encoded=json.dumps(name,ensure_ascii=False).replace('\\','\\\\').replace('"','\\"').replace('$','\\$').replace(';','\\;')
        (stage/'CMakeLists.txt').write_text(cmake.replace('@NAME@',encoded))
        module='game.dll' if sys.platform=='win32' else 'game.so'
        write_json(stage/'game.afterlight.project',{'format':'afterlight.project','version':1,'name':name,'startup_scene':'scenes/start.afterlight.json','game_module':'build/'+module})
        (stage/'.gitignore').write_text('/build/\n')
        (stage/'README.md').write_text(f'''# {name}

Build your C game module:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Open `game.afterlight.project` in the Afterlight editor, or run:
`afterlight_player --project /absolute/path/to/game.afterlight.project`.

WASD walks, mouse looks, Space jumps, R resets, Escape releases the pointer.
Edit game/game.c and rebuild to reload in editor Play. The standalone player
uses `--hot-reload` to opt into development reloading. The generated include/
headers match this engine build; refresh them when the engine ABI changes.

Copied templates, public headers and tools are MPL-2.0; see LICENSE and NOTICE.
Independent game files may use their own license. Building also creates the
module's .source.zip with marked MPL files and their modifications. Keep MPL
notices on covered files and provide their corresponding source when shipping.
''')
        stage.rename(destination)
    except BaseException:
        shutil.rmtree(stage,ignore_errors=True);raise
    return destination/'game.afterlight.project'
def read_json(path):
    raw=path.read_bytes()
    if len(raw)>16*1024*1024:raise ValueError(f'JSON is too large: {path}')
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError(f'Duplicate JSON field: {key}')
            result[key]=value
        return result
    return json.loads(raw.decode('utf-8'),object_pairs_hook=unique,parse_constant=lambda x:(_ for _ in ()).throw(ValueError('Non-finite JSON number')))
def export_project(project_path, destination, player_path, icd=None):
    import hashlib,struct,urllib.parse,stat,plistlib
    project_path=project_path.resolve();destination=destination.resolve();player_path=player_path.resolve()
    if destination.exists():raise ValueError('Export destination already exists; choose a new directory.')
    project=read_json(project_path)
    if project.get('format')!='afterlight.project' or project.get('version')!=1:raise ValueError('Unsupported Afterlight project.')
    if not player_path.exists():raise ValueError('Build the generic player before export (--player PATH).')
    module=(project_path.parent/project['game_module']).resolve();startup=(project_path.parent/project['startup_scene']).resolve()
    if not module.is_file() or not startup.is_file():raise ValueError('Build the C module and save the startup scene before exporting.')
    linux_libraries = []
    if sys.platform.startswith('linux'):
        # Check both binaries before preflight can load an instrumented module.
        for binary in (player_path, module):
            for line in subprocess.check_output(['ldd', str(binary)], text=True).splitlines():
                if '=>' not in line:
                    continue
                name, value = line.strip().split('=>', 1)
                name = pathlib.Path(name.strip()).name
                if name.startswith('libasan.') or 'clang_rt.asan' in name:
                    raise ValueError('Export a normal Release build, not an AddressSanitizer runtime.')
                if binary == player_path:
                    source = pathlib.Path(value.strip().split(' ', 1)[0])
                    system_library = name.startswith(('libc.', 'libm.', 'libdl.', 'libpthread.', 'librt.', 'ld-linux'))
                    if source.is_file() and not system_library:
                        linux_libraries.append((source, name))
    validator=player_path/'Contents/MacOS/afterlight_player' if player_path.is_dir() else player_path
    subprocess.run([str(validator),'--project',str(project_path),'--check'],check=True)
    destination.parent.mkdir(parents=True,exist_ok=True);stage=pathlib.Path(tempfile.mkdtemp(prefix='.afterlight-export-',dir=destination.parent))
    try:
        mac=player_path.is_dir() and player_path.suffix=='.app'
        if mac:
            app=stage/'Game.app';shutil.copytree(player_path,app,symlinks=False);resources=app/'Contents/Resources';executable=app/'Contents/MacOS/afterlight_player'
        else:
            app=None;resources=stage;executable=stage/player_path.name;shutil.copy2(player_path,executable)
            # CMake's non-bundle data is adjacent to the executable.
            for name in ['fonts','licenses','source']:
                source=player_path.parent/name
                if source.is_dir():shutil.copytree(source,resources/name)
            for name in ['LICENSE','NOTICE','THIRD_PARTY_NOTICES.md']:
                shutil.copy2(player_path.parent/name,resources/name)
        if not (resources/'source/afterlight-source.zip').is_file():
            raise ValueError('Player is missing its build-time MPL source archive; rebuild Afterlight before exporting.')
        for name in ['LICENSE','NOTICE','THIRD_PARTY_NOTICES.md']:
            if not (resources/name).is_file():raise ValueError(f'Player is missing {name}; rebuild Afterlight before exporting.')
        module_source=module.with_name(module.name+'.source.zip')
        if module_source.is_file():shutil.copy2(module_source,resources/'source/game-source.zip')
        (resources/'assets').mkdir(exist_ok=True);(resources/'scenes').mkdir(exist_ok=True);(resources/'game').mkdir(exist_ok=True)
        copied={};total=0
        def copy_asset(source):
            nonlocal total
            source=source.resolve()
            if source in copied:return copied[source]
            if not source.is_file():raise ValueError(f'Missing asset dependency: {source}')
            size=source.stat().st_size
            if size>64*1024*1024 or total+size>1024*1024*1024:raise ValueError('Export assets exceed file/total size limits.')
            total+=size;name=hashlib.sha256(str(source).encode()).hexdigest()[:16]+'_'+source.name;target=resources/'assets'/name;copied[source]=target
            extension=source.suffix.lower()
            if extension in ['.gltf','.glb']:
                binary=None
                if extension=='.glb':
                    raw=source.read_bytes()
                    if len(raw)<20 or raw[:4]!=b'glTF' or struct.unpack_from('<I',raw,8)[0]!=len(raw):raise ValueError('Invalid GLB while packaging.')
                    length,kind=struct.unpack_from('<II',raw,12)
                    if kind!=0x4e4f534a or 20+length>len(raw):raise ValueError('Invalid GLB JSON chunk.')
                    document=json.loads(raw[20:20+length]);binary=raw[20+length:]
                else:document=read_json(source)
                for item in document.get('buffers',[])+document.get('images',[]):
                    uri=item.get('uri')
                    if not uri or uri.startswith('data:'):continue
                    parsed=urllib.parse.urlsplit(uri)
                    if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:raise ValueError('Only local glTF dependency paths can be exported.')
                    dependency=copy_asset(source.parent/urllib.parse.unquote(parsed.path));item['uri']=urllib.parse.quote(dependency.name)
                if binary is None:write_json(target,document)
                else:
                    text=json.dumps(document,separators=(',',':')).encode();text+=b' '*((-len(text))%4)
                    target.write_bytes(struct.pack('<III',0x46546c67,2,20+len(text)+len(binary))+struct.pack('<II',len(text),0x4e4f534a)+text+binary)
            else:shutil.copy2(source,target)
            return target
        scenes=[startup]+sorted(p for p in startup.parent.glob('*.afterlight.json') if p.resolve()!=startup)
        if len(scenes)>256:raise ValueError('Too many scenes in startup directory.')
        startup_name=None
        for index,source in enumerate(scenes):
            document=read_json(source)
            if document.get('format')!='afterlight.scene' or document.get('version') not in range(1,25):raise ValueError(f'Unsupported scene: {source}')
            for asset in document.get('assets',[]):
                original,separator,selector=asset['path'].partition('#');packed=copy_asset(source.parent/original)
                asset['path']='../assets/'+packed.name+(separator+selector if separator else '')
            name=source.name;write_json(resources/'scenes'/name,document)
            if index==0:startup_name=name
        input_map=project_path.parent/'input.json'
        if input_map.is_file():shutil.copy2(input_map,resources/'input.json')
        prefabs=project_path.parent/'prefabs'
        if prefabs.is_dir():
            files=sorted(prefabs.rglob('*.afterlight.prefab'))
            if len(files)>128:raise ValueError('Too many prefabs (maximum 128).')
            for source in files:
                document=read_json(source)
                target=resources/'prefabs'/source.relative_to(prefabs);target.parent.mkdir(parents=True,exist_ok=True)
                for asset in document.get('assets',[]):
                    original,separator,selector=asset['path'].partition('#');packed=copy_asset(source.parent/original)
                    asset['path']=os.path.relpath(packed,target.parent)+(separator+selector if separator else '')
                write_json(target,document)
        game_module=resources/'game'/('game'+module.suffix);shutil.copy2(module,game_module)
        write_json(resources/'game.afterlight.project',{'format':'afterlight.project','version':1,'name':project['name'],'startup_scene':'scenes/'+startup_name,'game_module':'game/'+game_module.name})
        libraries=[]
        if mac:
            frameworks=app/'Contents/Frameworks';frameworks.mkdir(exist_ok=True);mapping={};pending=[executable,game_module]
            def package_library(path):
                path=path.resolve()
                if path in mapping:return mapping[path]
                if not path.is_file():raise ValueError(f'Missing runtime library: {path}')
                if 'clang_rt.asan' in path.name:raise ValueError('Export a normal Release build, not an AddressSanitizer runtime.')
                target=frameworks/path.name
                if target.exists():raise ValueError(f'Runtime library name collision: {path.name}')
                shutil.copy2(path,target);target.chmod(target.stat().st_mode|stat.S_IWUSR);mapping[path]=target;pending.append(target);libraries.append(target)
                # Homebrew package notices live above the lib/ directory.
                for parent in [path.parent.parent,path.parent.parent.parent]:
                    for license_name in ['LICENSE','LICENSE.txt','LICENSE.md','COPYING','COPYING.txt','NOTICE','NOTICE.txt','NOTICE.md']:
                        notice=parent/license_name
                        if notice.is_file():
                            folder=resources/'licenses'/path.stem;folder.mkdir(parents=True,exist_ok=True);shutil.copy2(notice,folder/license_name)
                return target
            candidates=[pathlib.Path(icd)] if icd else [pathlib.Path('/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json'),pathlib.Path('/usr/local/share/vulkan/icd.d/MoltenVK_icd.json')]
            manifest=next((p for p in candidates if p.is_file()),None)
            if manifest is None:raise ValueError('MoltenVK ICD not found; pass --icd /path/to/MoltenVK_icd.json.')
            driver=read_json(manifest);driver_path=(manifest.parent/driver['ICD']['library_path']).resolve();packed_driver=package_library(driver_path)
            driver['ICD']['library_path']='../../Frameworks/'+packed_driver.name;(resources/'vulkan').mkdir(exist_ok=True);write_json(resources/'vulkan/icd.json',driver)
            position=0
            while position<len(pending):
                binary=pending[position];position+=1
                dependencies=subprocess.check_output(['otool','-L',str(binary)],text=True).splitlines()[1:]
                for line in dependencies:
                    old=line.strip().split(' (',1)[0]
                    if old.startswith(('/usr/lib/','/System/Library/')):continue
                    if old.startswith('@executable_path/'):
                        # Already-local libraries in a previously packaged player.
                        continue
                    if old.startswith('@loader_path/'):source=binary.parent/old[len('@loader_path/'):]
                    elif old.startswith('@rpath/'):
                        # Resolve rpaths from the unmodified source when possible.
                        origin=module if binary==game_module else player_path/'Contents/MacOS/afterlight_player' if binary==executable else next((s for s,t in mapping.items() if t==binary),binary)
                        load=subprocess.check_output(['otool','-l',str(origin)],text=True);paths=[];lines=load.splitlines()
                        for at,text in enumerate(lines):
                            if 'cmd LC_RPATH' in text and at+2<len(lines):paths.append(lines[at+2].strip().split('path ',1)[-1].split(' (',1)[0])
                        source=next((pathlib.Path(p.replace('@loader_path',str(origin.parent)).replace('@executable_path',str((player_path/'Contents/MacOS').resolve())))/old[7:] for p in paths if (pathlib.Path(p.replace('@loader_path',str(origin.parent)).replace('@executable_path',str((player_path/'Contents/MacOS').resolve())))/old[7:]).exists()),None)
                        if source is None:raise ValueError(f'Cannot resolve runtime dependency {old}')
                    else:source=pathlib.Path(old)
                    # dylibs list their own install name as the first dependency.
                    origin=next((s for s,t in mapping.items() if t==binary),None)
                    if origin and source.resolve()==origin:continue
                    target=package_library(source)
                    subprocess.run(['install_name_tool','-change',old,'@executable_path/../Frameworks/'+target.name,str(binary)],check=True,stdout=subprocess.DEVNULL)
                if binary.parent==frameworks:subprocess.run(['install_name_tool','-id','@executable_path/../Frameworks/'+binary.name,str(binary)],check=True)
            vulkan=next((p for p in libraries if p.name.startswith('libvulkan.')),None)
            if vulkan and not (frameworks/'libvulkan.1.dylib').exists():(frameworks/'libvulkan.1.dylib').symlink_to(vulkan.name)
            info_path=app/'Contents/Info.plist';info=plistlib.loads(info_path.read_bytes());info['CFBundleDisplayName']=project['name'];info['CFBundleName']=project['name'];info_path.write_bytes(plistlib.dumps(info))
            for library in libraries:subprocess.run(['codesign','--force','--sign','-',str(library)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            subprocess.run(['codesign','--force','--sign','-',str(game_module)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            subprocess.run(['codesign','--force','--sign','-',str(app)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        elif sys.platform=='win32':
            for dll in player_path.parent.glob('*.dll'):shutil.copy2(dll,resources/dll.name)
        else:
            # Hardware Vulkan drivers are supplied by the destination system.
            library_dir=stage/'lib';library_dir.mkdir()
            for source, name in linux_libraries:
                shutil.copy2(source, library_dir / name)
            launcher=stage/'run.sh';launcher.write_text('#!/bin/sh\ncd -- "$(dirname -- "$0")"\nexport LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexec ./'+executable.name+' --project ./game.afterlight.project "$@"\n');launcher.chmod(0o755)
        write_json(stage/'export-manifest.json',{'format':'afterlight.export','version':1,'project':project['name'],'platform':sys.platform,'scenes':len(scenes),'assets':len(copied),'asset_bytes':total,'runtime_libraries':[p.name for p in libraries]})
        (stage/'README.txt').write_text('Afterlight export\n\n'+('Open Game.app to play.\n' if mac else 'Run '+('afterlight_player.exe\n' if sys.platform=='win32' else './run.sh\n'))+'\nThis build targets the exporting OS/CPU. macOS builds are ad-hoc signed; distribution notarization requires your own developer identity.\n\nAfterlight covered source is under MPL-2.0. '+('Inside Game.app/Contents/Resources/' if mac else 'Beside the executable, ')+'source/afterlight-source.zip contains the corresponding engine source. See LICENSE, NOTICE and THIRD_PARTY_NOTICES.md in that same resource directory; dependency licenses are in licenses/. Keep source and notices with distributions.\n'+('source/game-source.zip contains the game module\'s covered source.\n' if module_source.is_file() else 'The repository sample modules are included in the engine source archive. For a custom covered game module, the distributor must also provide its corresponding modified source.\n'))
        stage.rename(destination)
    except BaseException:
        shutil.rmtree(stage,ignore_errors=True);raise
    return destination

def bundle_project(project_path, destination, player_path, extensions=None, icd=None):
    import hashlib, struct, urllib.parse, stat
    project_path = project_path.resolve()
    destination = destination.resolve()
    player_path = player_path.resolve()
    if destination.is_dir():
        destination = destination / ('game.exe' if sys.platform == 'win32' else 'game')
    project = read_json(project_path)
    if project.get('format') != 'afterlight.project' or project.get('version') != 1:
        raise ValueError('Unsupported Afterlight project.')
    if not player_path.exists():
        raise ValueError('Build the generic player before bundling (--player PATH).')
    module = (project_path.parent / project['game_module']).resolve()
    startup = (project_path.parent / project['startup_scene']).resolve()
    if not module.is_file() or not startup.is_file():
        raise ValueError('Build the C module and save the startup scene before bundling.')

    if sys.platform.startswith('linux'):
        for binary in (player_path, module):
            for line in subprocess.check_output(['ldd', str(binary)], text=True).splitlines():
                if '=>' not in line:
                    continue
                name, value = line.strip().split('=>', 1)
                name = pathlib.Path(name.strip()).name
                if name.startswith('libasan.') or 'clang_rt.asan' in name:
                    raise ValueError('Export a normal Release build, not an AddressSanitizer runtime.')

    validator = player_path / 'Contents/MacOS/afterlight_player' if player_path.is_dir() else player_path
    subprocess.run([str(validator), '--project', str(project_path), '--check'], check=True)

    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = pathlib.Path(tempfile.mkdtemp(prefix='.afterlight-bundle-', dir=destination.parent if destination.parent.exists() else None))
    try:
        (stage / 'assets').mkdir(exist_ok=True)
        (stage / 'scenes').mkdir(exist_ok=True)
        (stage / 'game').mkdir(exist_ok=True)
        (stage / 'fonts').mkdir(exist_ok=True)
        (stage / 'source').mkdir(exist_ok=True)

        font_candidates = [
            player_path / 'Contents/Resources/fonts/FiraSans-Regular.ttf' if player_path.is_dir() else player_path.parent / 'fonts/FiraSans-Regular.ttf',
            player_path.parent / 'fonts/FiraSans-Regular.ttf',
            ROOT / 'editor/assets/FiraSans-Regular.ttf',
            ROOT / 'sandbox/fonts/FiraSans-Regular.ttf',
        ]
        font_file = next((f for f in font_candidates if f.is_file()), None)
        if font_file:
            shutil.copy2(font_file, stage / 'fonts/FiraSans-Regular.ttf')

        source_candidates = [
            player_path / 'Contents/Resources/source/afterlight-source.zip' if player_path.is_dir() else player_path.parent / 'source/afterlight-source.zip',
            ROOT / 'build/release/source/afterlight-source.zip',
            ROOT / 'build/debug/source/afterlight-source.zip',
        ]
        source_file = next((s for s in source_candidates if s.is_file()), None)
        if source_file:
            shutil.copy2(source_file, stage / 'source/afterlight-source.zip')

        for name in ['LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md']:
            for cand in [player_path / 'Contents/Resources' / name if player_path.is_dir() else player_path.parent / name, ROOT / name]:
                if cand.is_file():
                    shutil.copy2(cand, stage / name)
                    break

        module_source = module.with_name(module.name + '.source.zip')
        if module_source.is_file():
            shutil.copy2(module_source, stage / 'source/game-source.zip')

        copied = {}
        total = 0
        def copy_asset(source):
            nonlocal total
            source = source.resolve()
            if source in copied:
                return copied[source]
            if not source.is_file():
                raise ValueError(f'Missing asset dependency: {source}')
            size = source.stat().st_size
            if size > 64 * 1024 * 1024 or total + size > 1024 * 1024 * 1024:
                raise ValueError('Export assets exceed file/total size limits.')
            total += size
            name = hashlib.sha256(str(source).encode()).hexdigest()[:16] + '_' + source.name
            target = stage / 'assets' / name
            copied[source] = target
            extension = source.suffix.lower()
            if extension in ['.gltf', '.glb']:
                binary = None
                if extension == '.glb':
                    raw = source.read_bytes()
                    if len(raw) < 20 or raw[:4] != b'glTF' or struct.unpack_from('<I', raw, 8)[0] != len(raw):
                        raise ValueError('Invalid GLB while packaging.')
                    length, kind = struct.unpack_from('<II', raw, 12)
                    if kind != 0x4e4f534a or 20 + length > len(raw):
                        raise ValueError('Invalid GLB JSON chunk.')
                    document = json.loads(raw[20:20 + length])
                    binary = raw[20 + length:]
                else:
                    document = read_json(source)
                for item in document.get('buffers', []) + document.get('images', []):
                    uri = item.get('uri')
                    if not uri or uri.startswith('data:'):
                        continue
                    parsed = urllib.parse.urlsplit(uri)
                    if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
                        raise ValueError('Only local glTF dependency paths can be exported.')
                    dependency = copy_asset(source.parent / urllib.parse.unquote(parsed.path))
                    item['uri'] = urllib.parse.quote(dependency.name)
                if binary is None:
                    write_json(target, document)
                else:
                    text = json.dumps(document, separators=(',', ':')).encode()
                    text += b' ' * ((-len(text)) % 4)
                    target.write_bytes(struct.pack('<III', 0x46546c67, 2, 20 + len(text) + len(binary)) +
                                       struct.pack('<II', len(text), 0x4e4f534a) + text + binary)
            else:
                shutil.copy2(source, target)
            return target

        scenes = [startup] + sorted(p for p in startup.parent.glob('*.afterlight.json') if p.resolve() != startup)
        if len(scenes) > 256:
            raise ValueError('Too many scenes in startup directory.')
        startup_name = None
        for index, source in enumerate(scenes):
            document = read_json(source)
            if document.get('format') != 'afterlight.scene' or document.get('version') not in range(1, 25):
                raise ValueError(f'Unsupported scene: {source}')
            for asset in document.get('assets', []):
                original, separator, selector = asset['path'].partition('#')
                packed = copy_asset(source.parent / original)
                asset['path'] = '../assets/' + packed.name + (separator + selector if separator else '')
            name = source.name
            write_json(stage / 'scenes' / name, document)
            if index == 0:
                startup_name = name

        input_map = project_path.parent / 'input.json'
        if input_map.is_file():
            shutil.copy2(input_map, stage / 'input.json')
        prefabs = project_path.parent / 'prefabs'
        if prefabs.is_dir():
            files = sorted(prefabs.rglob('*.afterlight.prefab'))
            if len(files) > 128:
                raise ValueError('Too many prefabs (maximum 128).')
            for source in files:
                document = read_json(source)
                target = stage / 'prefabs' / source.relative_to(prefabs)
                target.parent.mkdir(parents=True, exist_ok=True)
                for asset in document.get('assets', []):
                    original, separator, selector = asset['path'].partition('#')
                    packed = copy_asset(source.parent / original)
                    asset['path'] = os.path.relpath(packed, target.parent) + (separator + selector if separator else '')
                write_json(target, document)

        game_module = stage / 'game' / ('game' + module.suffix)
        shutil.copy2(module, game_module)
        write_json(stage / 'game.afterlight.project', {
            'format': 'afterlight.project',
            'version': 1,
            'name': project['name'],
            'startup_scene': 'scenes/' + startup_name,
            'game_module': 'game/' + game_module.name
        })

        ext_list = []
        if extensions:
            for ext in extensions:
                ext_path = pathlib.Path(ext).resolve()
                if ext_path.is_file() and ext_path not in ext_list:
                    ext_list.append(ext_path)
        for ext_folder in [project_path.parent / 'extensions', player_path.parent / 'extensions']:
            if ext_folder.is_dir():
                for ext in sorted(ext_folder.glob('*')):
                    if ext.is_file() and ext.suffix.lower() in ['.so', '.dylib', '.dll']:
                        if ext not in ext_list:
                            ext_list.append(ext)
        if ext_list:
            (stage / 'extensions').mkdir(exist_ok=True)
            for ext in ext_list:
                shutil.copy2(ext, stage / 'extensions' / ext.name)

        entries = []
        for root, dirs, files in os.walk(stage):
            for f in files:
                full = pathlib.Path(root) / f
                rel = full.relative_to(stage).as_posix()
                data = full.read_bytes()
                mode = full.stat().st_mode & 0o777
                entries.append((rel, mode, data))
        entries.sort(key=lambda x: x[0])

        payload = bytearray()
        payload.extend(b'AFBUNDLE')
        payload.extend(struct.pack('<Q', len(entries)))
        for rel, mode, data in entries:
            rel_bytes = rel.encode('utf-8')
            payload.extend(struct.pack('<H', len(rel_bytes)))
            payload.extend(rel_bytes)
            payload.extend(struct.pack('<QI', len(data), mode))
            payload.extend(data)

        executable = player_path / 'Contents/MacOS/afterlight_player' if player_path.is_dir() else player_path
        if destination.exists():
            destination.unlink()
        shutil.copy2(executable, destination)

        if sys.platform == 'darwin':
            bundle_bin = stage / 'bundle.bin'
            bundle_bin.write_bytes(payload)
            llvm_objcopy = shutil.which('llvm-objcopy')
            if not llvm_objcopy:
                try:
                    res = subprocess.run(['xcrun', '-f', 'llvm-objcopy'], capture_output=True, text=True)
                    if res.returncode == 0 and res.stdout.strip():
                        llvm_objcopy = res.stdout.strip()
                except Exception:
                    pass
            if not llvm_objcopy:
                for candidate in ['/opt/homebrew/opt/llvm/bin/llvm-objcopy', '/usr/local/opt/llvm/bin/llvm-objcopy']:
                    if pathlib.Path(candidate).is_file():
                        llvm_objcopy = candidate
                        break
            if llvm_objcopy:
                subprocess.run([llvm_objcopy, '--add-section', '__BUNDLE,__bundle=' + str(bundle_bin), str(destination)], check=True)
                subprocess.run(['codesign', '--force', '--sign', '-', str(destination)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                with open(destination, 'ab') as f:
                    offset = f.tell()
                    f.write(payload)
                    f.write(struct.pack('<QQQQ', 0x5F544C5245544641, 0x5F454C444E55425F, offset, len(entries)))
                try:
                    subprocess.run(['codesign', '--force', '--sign', '-', str(destination)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception:
                    pass
        else:
            with open(destination, 'ab') as f:
                offset = f.tell()
                f.write(payload)
                f.write(struct.pack('<QQQQ', 0x5F544C5245544641, 0x5F454C444E55425F, offset, len(entries)))

        destination.chmod(0o755)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    return destination

def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='command',required=True)
    new=commands.add_parser('new',help='Create a starter project in a new directory');new.add_argument('directory',type=pathlib.Path);new.add_argument('--name',required=True)
    export=commands.add_parser('export',help='Package a saved project and generic player');export.add_argument('project',type=pathlib.Path);export.add_argument('--output',required=True,type=pathlib.Path);export.add_argument('--player',required=True,type=pathlib.Path);export.add_argument('--icd',type=pathlib.Path)
    bundle=commands.add_parser('bundle',help='Bundle a saved project into a single standalone binary executable without external folders');bundle.add_argument('project',type=pathlib.Path);bundle.add_argument('--output',required=True,type=pathlib.Path);bundle.add_argument('--player',required=True,type=pathlib.Path);bundle.add_argument('--extensions',nargs='*',type=pathlib.Path,default=[]);bundle.add_argument('--icd',type=pathlib.Path)
    args=parser.parse_args()
    try:
        if args.command=='new':print(create_project(args.directory,args.name))
        elif args.command=='export':print(export_project(args.project,args.output,args.player,args.icd))
        elif args.command=='bundle':print(bundle_project(args.project,args.output,args.player,args.extensions,args.icd))
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f'Afterlight: {error}',file=sys.stderr);return 1
    return 0
if __name__=='__main__':sys.exit(main())

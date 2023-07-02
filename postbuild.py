FILENAME_BUILDNO = 'versioning'
FILENAME_VERSION_H = 'include/version.h'

# if this storage folder exists we copy results to it for later publish
file_directory = '/tmp/arskafiles/'
envs = ['esp32-generic-6ch']

import shutil
import os.path
from os import path
import os

# We may need params like PROJECT_BUILD_DIR later
#from SCons.Script import DefaultEnvironment  # pylint: disable=import-error
env = DefaultEnvironment()
#config = env.GetProjectConfig()
#PROJECT_BUILD_DIR=config.get("platformio", "build_dir")
#print(PROJECT_BUILD_DIR)


# we will wait the firmware.bin to be created 
def before_upload(source, target, env):
    #print("before_upload")
    # env.Dump()
    filesystem = env.GetProjectOption("board_build.filesystem")
    
    fs_filename = filesystem + ".bin"
    print (fs_filename)

    # do some actions
       # print("Post build. Building firmware.  DEFAULT_TARGETS:")
    build_no = 0
    version_base =""
    try:
        with open(FILENAME_BUILDNO) as f:
            build_no = int(f.readline()) 
    except:
        print('Starting build number from 1..')
        build_no = 1

    try:
        with open(FILENAME_VERSION_H) as f: #first line contains version commented
            version_base = f.readline().replace("//","").strip()
    except:
        print('Unknown version_base')
        exit(0)

    print ("version_base: [" + version_base + "]")

    # copy binary files for release, version based destination 
    if path.exists(file_directory) and path.isdir(file_directory):
        for env_id in envs:   
            compile_folder = '.pio/build/'+ env_id + '/'
            dest_dir = file_directory+env_id+"/"+version_base+"/"

            if not path.exists(dest_dir):
                os.makedirs(dest_dir,0o777,True)

            print (compile_folder+"firmware.bin"," -> ", dest_dir )
            shutil.copyfile(compile_folder+"firmware.bin", dest_dir+"firmware.bin")
            shutil.copyfile(compile_folder+fs_filename, dest_dir+fs_filename)
            shutil.copyfile(compile_folder+"partitions.bin", dest_dir+"partitions.bin")
            shutil.copyfile(compile_folder+"bootloader.bin", dest_dir+"bootloader.bin")

            #manifest_from_path =  "install/" + env_id + "/manifest.json"
            manifest_to_path = dest_dir + "manifest.json"
            #print (manifest_from_path + " --> "+manifest_to_path)
            #if path.exists(manifest_from_path) and not path.exists(manifest_to_path):
            #    shutil.copyfile(manifest_from_path, manifest_to_path)


            manifest_txt = """{
            "name": "Arska",
            "new_install_prompt_erase": true,
            "builds": [
            {
                "chipFamily": "ESP32",
                "parts": [
                {"path": "bootloader.bin", "offset" :4096},
                {"path": "partitions.bin", "offset" :32768},
                {"path": "../boot_app0.bin", "offset" :57344},
                { "path": "firmware.bin", "offset": 65536 },
                { "path": "FILESYSTEM_FILE_NAME", "offset": 2686976 }
                ]
            }
            ]
            }""".replace("FILESYSTEM_FILE_NAME",fs_filename)
            with open(manifest_to_path, 'w+') as f:
                f.write(manifest_txt)
            
            print("md5 -q "+ dest_dir+"bootloader.bin >" + dest_dir + "bootloader.md5")
            os.system("md5 -q "+ dest_dir+"bootloader.bin >" + dest_dir + "bootloader.md5" ) 


if "buildfs" in BUILD_TARGETS or "uploadfs" in BUILD_TARGETS or "uploadfsota" in BUILD_TARGETS:
    # No version/build increment for filesystem builds
    print("Post build. Building filesystem. No new version. BUILD_TARGETS:")
    print(BUILD_TARGETS)
else:


    env.AddPreAction("upload", before_upload)




            


FILENAME_BUILDNO = 'versioning'
FILENAME_VERSION_H = 'include/version.h'

# if this storage folder exists we copy results to it for later publish
file_directory = '/tmp/arskafiles/'
envs = ['esp32-generic-6ch']

import shutil
import os.path
from os import path

# We may need params like PROJECT_BUILD_DIR later
#from SCons.Script import DefaultEnvironment  # pylint: disable=import-error
env = DefaultEnvironment()
#config = env.GetProjectConfig()
#PROJECT_BUILD_DIR=config.get("platformio", "build_dir")
#print(PROJECT_BUILD_DIR)

# we will wait the firmware.bin to be created 
def before_upload(source, target, env):
    print("before_upload")
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
        with open(FILENAME_VERSION_H) as f:
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
            shutil.copyfile(compile_folder+"littlefs.bin", dest_dir+"littlefs.bin")
            shutil.copyfile(compile_folder+"partitions.bin", dest_dir+"partitions.bin")

            manifest_from_path =  "install/" + env_id + "/manifest.json"
            manifest_to_path = dest_dir + "manifest.json"
            print (manifest_from_path + " --> "+manifest_to_path)
            if path.exists(manifest_from_path) and not path.exists(manifest_to_path):
                shutil.copyfile(manifest_from_path, manifest_to_path)
               



if "buildfs" in BUILD_TARGETS or "uploadfs" in BUILD_TARGETS or "uploadfsota" in BUILD_TARGETS:
    # No version/build increment for filesystem builds
    print("Post build. Building filesystem. No new version. BUILD_TARGETS:")
    print(BUILD_TARGETS)
else:


    env.AddPreAction("upload", before_upload)




            


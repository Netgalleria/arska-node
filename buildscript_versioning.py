# https://stackoverflow.com/questions/56923895/auto-increment-build-number-using-platformio

FILENAME_BUILDNO = 'versioning'
FILENAME_VERSION_H = 'include/version.h'
# main version, ends with . (point) , build added automatically to the end
# phases alpha, beta, rc, stable,  
# e.g. 0.93.0-alpha1,  0.93.0-beta1,  0.93.0-rc1,  0.93.0-stable, 0.93.1-stable  

version = '0.93.0-beta2'
#version = '0.92.0-rc2'
#version = '0.92.0-stable'

if version[-1]==".":
  version_base = version[:len(version)-1]
else:
  version_base = version


import datetime

if "buildfs" in BUILD_TARGETS or "uploadfs" in BUILD_TARGETS or "uploadfsota" in BUILD_TARGETS:
  print("Building filesystem. No new version. BUILD_TARGETS:")

  print(BUILD_TARGETS)
else:
  print("Not buildfs. New version.")

  #print("Current CLI targets", COMMAND_LINE_TARGETS) #buildfs, upload, uploadfs, uploadfsota
  #print("Current Build targets", BUILD_TARGETS)

  build_no = 0
  try:
      with open(FILENAME_BUILDNO) as f:
          build_no = int(f.readline()) + 1
  except:
      print('Starting build number from 1..')
      build_no = 1
  with open(FILENAME_BUILDNO, 'w+') as f:
      f.write(str(build_no))
      print('Build number: {}'.format(build_no))

  version_with_build = version_base+"."+str(build_no)

  hf = """//{}
  #ifndef VERSION_BASE
    #define VERSION_BASE "{}"
  #endif
  #ifndef BUILD_NUMBER
    #define BUILD_NUMBER "{}"
  #endif
  #ifndef VERSION
    #define VERSION "{} - {}"
  #endif
  #ifndef VERSION_SHORT
    #define VERSION_SHORT "{}"
  #endif
  """.format(version_base,version_base, build_no, version_with_build, str(datetime.datetime.now())[:19], version_with_build)
  with open(FILENAME_VERSION_H, 'w+') as f:
      f.write(hf)
  # FS  versioning added by Olli Rinne 2022
  FILENAME_VERSION_FS = 'data/data/version.txt'
  hffs = """{} - {}
  """.format(version_with_build, str(datetime.datetime.now())[:19])
  with open(FILENAME_VERSION_FS, 'w+') as f:
      f.write(hffs)

  import os
  print("Rebuilding filesystem file littlefs.bin")
  returned_value = os.system("platformio run --target buildfs --environment esp32-generic-6ch")
    
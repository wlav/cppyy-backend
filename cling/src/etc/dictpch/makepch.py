#! /usr/bin/env python
#
# Build a pch for the headers and linkdefs in root-build-dir/etc/dictpch/.
# root-build-dir is first tried as ./ - if that doesn't exist, $ROOTSYS
# is taken as root-build-dir.
#
# $1: PCH output file name
# $2: cxxflags (optional; required if extra headers are supplied)
# $3: extra headers to be included in the PCH (optional)
#
# exit code 1 for invocation errors; else exit code of rootcling invocation.
#
# Copyright (c) 2014 Rene Brun and Fons Rademakers
# Author: Axel Naumann <axel@cern.ch>, 2014-10-16
# Translated to python by Danilo Piparo, 2015-4-23


from __future__ import print_function
import sys
import os
import subprocess
import shutil
import tempfile

#-------------------------------------------------------------------------------
def getArgs():
   argv = sys.argv
   argc = len(argv)
   if argc < 2:
      print("ERROR: too few arguments specified!")
   pchFileName = argv[1]
   cxxflags = ""
   if argc > 2:
      cxxflags = argv[2]
   extraHeadersList = ""
   if argc > 3:
      extraHeadersList = argv[2:]
   return pchFileName, cxxflags, extraHeadersList

#-------------------------------------------------------------------------------
def getCppFlags(cppflagsFilename):
   ifile = open(cppflagsFilename)
   lines = ifile.readlines()
   ifile.close()
   stdcxx = os.environ.get("STDCXX", "")
   if stdcxx or "std" in os.environ.get("EXTRA_CLING_ARGS", ""):
      keep = []
      for line in lines:
         if not "std" in line:
            keep.append(line)
      lines = keep
   try:
      lines.remove("-fno-plt\n")       # unsupported by Cling
   except ValueError:
      pass
   cppFlags = [line[:-1].strip() for line in lines]
   if stdcxx:
      if stdcxx == "20":
         stdcxx = "2a"
      cppFlags.append("-std=c++"+stdcxx)

   return cppFlags


#-------------------------------------------------------------------------------
def makepch():
   """
   Create a pch starting from the following arguments
   1) pch file name
   2) Compiler flags - optional, required if extra headers are supplied
   3) Extra headers - optional
   """
   rootdir = "."
   cfgdir = os.path.join("etc","dictpch")
   allheadersFilename = os.path.join(cfgdir,"allHeaders.h")
   alllinkdefsFilename = os.path.join(cfgdir,"allLinkDefs.h")
   cppflagsFilename = os.path.join(cfgdir,"allCppflags.txt")

   pchFileName, extraCppflags, extraHeadersList = getArgs()

   rootbuildFlag=""
   loc1 = os.path.join(rootdir, allheadersFilename)
   rootsys = ""
   rootSysEnvName = "ROOTSYS"
   if rootSysEnvName in os.environ:
      rootsys = os.environ[rootSysEnvName]
   loc2 = os.path.join(rootsys, allheadersFilename)

   if not os.path.exists(loc1):
      rootdir = rootsys
      if not os.path.exists(loc2):
         print ("ERROR: cannot find %s file here %s nor here %s" %(allheadersFilename, loc1, loc2))
         sys.exit(1)
   else:
      rootbuildFlag="-rootbuild"

   macrosList=["-D__CLING__",
                 "-DROOT_PCH",
                 "-I%s" %os.path.join(rootdir,"include"),
                 "-I%s" %os.path.join(rootdir,"etc"),
                 "-I%s" %os.path.join(rootdir,cfgdir),
                 "-I%s" %os.path.join(rootdir,"etc","cling"),
                ]

   macrosList.append(extraCppflags)

   if sys.platform == 'win32':
      allheadersFilename.replace("\\","/")
      alllinkdefsFilename.replace("\\","/")

   rootclingExe = os.path.join(rootdir,"bin","rootcling")
   tmpdir = tempfile.gettempdir()
   outf = os.path.join(tmpdir, "allDict.cxx")
   command = [rootclingExe,
              rootbuildFlag,
              "-generate-pch",
              "-f", outf,
              "-noDictSelection",
             ]
   command += macrosList
   cppFlagsList = getCppFlags(cppflagsFilename)
   try:
      cppFlagsList.remove("-isystem")
   except ValueError:   # "-isystem" not in list
      pass

   command.append("-cxxflags")
   command.append(" ".join(cppFlagsList))

   command.append(allheadersFilename)
   command += extraHeadersList
   command.append(alllinkdefsFilename)

   if "VERBOSE" in os.environ:
      print(command)

   my_env = os.environ.copy()
   existing_ldlib = my_env.get("LD_LIBRARY_PATH")
   if not existing_ldlib: existing_ldlib = ""
   my_env["LD_LIBRARY_PATH"] = os.path.join(rootdir, "lib") + ":" + existing_ldlib

   ret = subprocess.call(command, env=my_env)
   if ret == 0:
      shutil.move(os.path.join(tmpdir, "allDict_rdict.pch"), pchFileName)
      os.unlink(outf)

   return ret

if __name__ == "__main__":
   ret = makepch()
   sys.exit(ret)

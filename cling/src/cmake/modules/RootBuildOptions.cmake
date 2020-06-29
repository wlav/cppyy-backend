# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

set(root_build_options)

#---------------------------------------------------------------------------------------------------
#---ROOT_BUILD_OPTION( name defvalue [description] )
#---------------------------------------------------------------------------------------------------
function(ROOT_BUILD_OPTION opt defvalue)
  if(ARGN)
    set(description ${ARGN})
  else()
    set(description " ")
  endif()
  set(${opt}_defvalue    ${defvalue} PARENT_SCOPE)
  set(${opt}_description ${description} PARENT_SCOPE)
  set(root_build_options  ${root_build_options} ${opt} PARENT_SCOPE )
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_APPLY_OPTIONS()
#---------------------------------------------------------------------------------------------------
function(ROOT_APPLY_OPTIONS)
  foreach(opt ${root_build_options})
     option(${opt} "${${opt}_description}" ${${opt}_defvalue})
  endforeach()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_GET_OPTIONS(result ENABLED)
#---------------------------------------------------------------------------------------------------
function(ROOT_GET_OPTIONS result)
  CMAKE_PARSE_ARGUMENTS(ARG "ENABLED" "" "" ${ARGN})
  set(enabled)
  foreach(opt ${root_build_options})
    if(ARG_ENABLED)
      if(${opt})
        set(enabled "${enabled} ${opt}")
      endif()
    else()
      set(enabled "${enabled} ${opt}")
    endif()
  endforeach()
  set(${result} "${enabled}" PARENT_SCOPE)
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_SHOW_ENABLED_OPTIONS()
#---------------------------------------------------------------------------------------------------
function(ROOT_SHOW_ENABLED_OPTIONS)
  set(enabled_opts)
  ROOT_GET_OPTIONS(enabled_opts ENABLED)
  foreach(opt ${enabled_opts})
    message(STATUS "Enabled support for: ${opt}")
  endforeach()
endfunction()

#---------------------------------------------------------------------------------------------------
#---ROOT_WRITE_OPTIONS(file )
#---------------------------------------------------------------------------------------------------
function(ROOT_WRITE_OPTIONS file)
  file(WRITE ${file} "#---Options enabled for the build of ROOT-----------------------------------------------\n")
  foreach(opt ${root_build_options})
    if(${opt})
      file(APPEND ${file} "set(${opt} ON)\n")
    else()
      file(APPEND ${file} "set(${opt} OFF)\n")
    endif()
  endforeach()
endfunction()

#--------------------------------------------------------------------------------------------------
#---Full list of options with their descriptions and default values
#   The default value can be changed as many times as we wish before calling ROOT_APPLY_OPTIONS()
#--------------------------------------------------------------------------------------------------

ROOT_BUILD_OPTION(builtin_clang ON "Build bundled copy of Clang")
ROOT_BUILD_OPTION(builtin_llvm ON "Build bundled copy of LLVM")
ROOT_BUILD_OPTION(builtin_zlib OFF "Build bundled copy of zlib")
ROOT_BUILD_OPTION(cuda OFF "Enable support for CUDA (requires CUDA toolkit >= 7.5)")
ROOT_BUILD_OPTION(cudnn ON "Enable support for cuDNN (default when Cuda is enabled)")
ROOT_BUILD_OPTION(cxxmodules OFF "Enable support for C++ modules")
ROOT_BUILD_OPTION(exceptions ON "Enable compiler exception handling")
ROOT_BUILD_OPTION(gnuinstall OFF "Perform installation following the GNU guidelines")
ROOT_BUILD_OPTION(libcxx OFF "Build using libc++")
ROOT_BUILD_OPTION(rpath OFF "Link libraries with built-in RPATH (run-time search path)")
ROOT_BUILD_OPTION(runtime_cxxmodules ON "Enable runtime support for C++ modules")
ROOT_BUILD_OPTION(shadowpw OFF "Enable support for shadow passwords")
ROOT_BUILD_OPTION(shared ON "Use shared 3rd party libraries if possible")
ROOT_BUILD_OPTION(soversion OFF "Set version number in sonames (recommended)")
ROOT_BUILD_OPTION(winrtdebug OFF "Link against the Windows debug runtime library")

option(all "Enable all optional components by default" OFF)
option(clingtest "Enable cling tests (Note: that this makes llvm/clang symbols visible in libCling)" OFF)
option(fail-on-missing "Fail at configure time if a required package cannot be found" OFF)
option(gminimal "Enable only required options by default, but include X11" OFF)
option(minimal "Enable only required options by default" OFF)
option(rootbench "Build rootbench if rootbench exists in root or if it is a sibling directory." OFF)
option(roottest "Build roottest if roottest exists in root or if it is a sibling directory." OFF)
option(testing "Enable testing with CTest" OFF)

set(gcctoolchain "" CACHE PATH "Set path to GCC toolchain used to build llvm/clang")

if(all AND minimal)
  message(FATAL_ERROR "The 'all' and 'minimal' options are mutually exclusive")
endif()

#--- Compression algorithms in ROOT-------------------------------------------------------------
set(compression_default "zlib" CACHE STRING "Default compression algorithm (zlib (default), lz4, zstd or lzma)")
string(TOLOWER "${compression_default}" compression_default)
if("${compression_default}" MATCHES "zlib|lz4|lzma|zstd")
  message(STATUS "ROOT default compression algorithm: ${compression_default}")
else()
  message(FATAL_ERROR "Unsupported compression algorithm: ${compression_default}\n"
    "Known values are zlib, lzma, lz4, zstd (case-insensitive).")
endif()

#---Changes in defaults due to platform-------------------------------------------------------
if(WIN32)
  set(runtime_cxxmodules_defvalue OFF)
elseif(APPLE)
  set(runtime_cxxmodules_defvalue OFF)
endif()

# Current limitations for modules:
#---Modules are disabled on aarch64 platform (due ODR violations)
if(CMAKE_SYSTEM_PROCESSOR MATCHES aarch64)
  set(runtime_cxxmodules_defvalue OFF)
endif()


#---Apply minimal or gminimal------------------------------------------------------------------
foreach(opt ${root_build_options})
  if(NOT opt MATCHES "builtin_llvm|builtin_clang|shared")
    if(minimal)
      set(${opt}_defvalue OFF)
    elseif(gminimal)
      set(${opt}_defvalue OFF)
    endif()
  endif()
endforeach()

#---Define at moment the options with the selected default values-----------------------------
ROOT_APPLY_OPTIONS()

#---roottest option implies testing
if(roottest OR rootbench)
  set(testing ON CACHE BOOL "" FORCE)
endif()

#---Avoid creating dependencies to 'non-standard' header files -------------------------------
include_regular_expression("^[^.]+$|[.]h$|[.]icc$|[.]hxx$|[.]hpp$")

#---Add Installation Variables------------------------------------------------------------------
include(RootInstallDirs)

#---RPATH options-------------------------------------------------------------------------------
#  When building, don't use the install RPATH already (but later on when installing)
set(CMAKE_SKIP_BUILD_RPATH FALSE)         # don't skip the full RPATH for the build tree
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) # use always the build RPATH for the build tree
set(CMAKE_MACOSX_RPATH TRUE)              # use RPATH for MacOSX
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE) # point to directories outside the build tree to the install RPATH

# Check whether to add RPATH to the installation (the build tree always has the RPATH enabled)
if(rpath)
  set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}) # install LIBDIR
  set(CMAKE_SKIP_INSTALL_RPATH FALSE)          # don't skip the full RPATH for the install tree
elseif(APPLE)
  set(CMAKE_INSTALL_NAME_DIR "@rpath")
  if(gnuinstall)
    set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}) # install LIBDIR
  else()
    set(CMAKE_INSTALL_RPATH "@loader_path/../lib")    # self relative LIBDIR
  endif()
  set(CMAKE_SKIP_INSTALL_RPATH FALSE)          # don't skip the full RPATH for the install tree
else()
  set(CMAKE_SKIP_INSTALL_RPATH TRUE)           # skip the full RPATH for the install tree
endif()

#---deal with the DCMAKE_IGNORE_PATH------------------------------------------------------------
if(macos_native)
  if(APPLE)
    set(CMAKE_IGNORE_PATH)
    foreach(_prefix /sw /opt/local /usr/local) # Fink installs in /sw, and MacPort in /opt/local and Brew in /usr/local
      list(APPEND CMAKE_IGNORE_PATH ${_prefix}/bin ${_prefix}/include ${_prefix}/lib)
    endforeach()
  else()
    message(STATUS "Option 'macos_native' is only for MacOS systems. Ignoring it.")
  endif()
endif()

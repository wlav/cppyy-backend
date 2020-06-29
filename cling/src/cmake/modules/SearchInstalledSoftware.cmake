# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

#---Check for installed packages depending on the build options/components enabled --
include(ExternalProject)
include(FindPackageHandleStandardArgs)

set(lcgpackages http://lcgpackages.web.cern.ch/lcgpackages/tarFiles/sources)

macro(find_package)
  if(NOT "${ARGV0}" IN_LIST ROOT_BUILTINS)
    _find_package(${ARGV})
  endif()
endmacro()

#---On MacOSX, try to find frameworks after standard libraries or headers------------
set(CMAKE_FIND_FRAMEWORK LAST)

#---If -Dshared=Off, prefer static libraries-----------------------------------------
if(NOT shared)
  if(WINDOWS)
    message(FATAL_ERROR "Option \"shared=Off\" not supported on Windows!")
  else()
    message("Preferring static libraries.")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;${CMAKE_FIND_LIBRARY_SUFFIXES}")
  endif()
endif()

#---Check for Zlib ------------------------------------------------------------------
if(NOT builtin_zlib)
  message(STATUS "Looking for ZLib")
  # Clear cache variables, or LLVM may use old values for ZLIB
  foreach(suffix FOUND INCLUDE_DIR LIBRARY LIBRARY_DEBUG LIBRARY_RELEASE)
    unset(ZLIB_${suffix} CACHE)
  endforeach()
  if(fail-on-missing)
    find_package(ZLIB REQUIRED)
  else()
    find_package(ZLIB)
    if(NOT ZLIB_FOUND)
      message(STATUS "Zlib not found. Switching on builtin_zlib option")
      set(builtin_zlib ON CACHE BOOL "Enabled because Zlib not found (${builtin_zlib_description})" FORCE)
    endif()
  endif()
endif()

if(builtin_zlib)
  list(APPEND ROOT_BUILTINS ZLIB)
  add_subdirectory(builtins/zlib)
endif()

#---Check for cling and llvm --------------------------------------------------------

set(CLING_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/interpreter/cling/include)

if(MSVC)
  set(CLING_CXXFLAGS "-DNOMINMAX -D_XKEYCHECK_H")
else()
  set(CLING_CXXFLAGS "-fvisibility=hidden -Wno-shadow -fno-strict-aliasing -Wno-unused-parameter -Wwrite-strings -Wno-long-long")
endif()
if (CMAKE_COMPILER_IS_GNUCXX)
  set(CLING_CXXFLAGS "${CLING_CXXFLAGS} -Wno-missing-field-initializers")
endif()
#---These are the libraries that we link ROOT with CLING---------------------------
set(CLING_LIBRARIES clingInterpreter clingMetaProcessor clingUtils)
add_custom_target(CLING)
add_dependencies(CLING ${CLING_LIBRARIES})
if (builtin_llvm)
  add_dependencies(CLING intrinsics_gen)
endif()
if (builtin_clang)
  add_dependencies(CLING clang-headers)
endif()

#---Check for CUDA-----------------------------------------------------------------------
# if tmva-gpu is off and cuda is on cuda is searched but not used in tmva
#  if cuda is off but tmva-gpu is on cuda is searched and activated if found !
#
if(cuda OR tmva-gpu)
  find_package(CUDA)
  if(CUDA_FOUND)
    if(NOT DEFINED CMAKE_CUDA_STANDARD)
      set(CMAKE_CUDA_STANDARD ${CMAKE_CXX_STANDARD})
    endif()
    enable_language(CUDA)
    set(cuda ON CACHE BOOL "Found Cuda for TMVA GPU" FORCE)
    ###
    ### look for package CuDNN
    if (cudnn)
      if (fail-on-missing)
        find_package(CuDNN REQUIRED)
      else()
        find_package(CuDNN)
      endif()
      if (CUDNN_FOUND)
        message(STATUS "CuDNN library found: " ${CUDNN_LIBRARIES})
	### set tmva-cudnn flag only if tmva-gpu is on!
        if (tmva-gpu)
          set(tmva-cudnn ON)
        endif()
      else()
        message(STATUS "CuDNN library not found")
        set(cudnn OFF CACHE BOOL "Disabled because cudnn is not found" FORCE)
      endif()
    endif()
  elseif(fail-on-missing)
    message(FATAL_ERROR "CUDA not found. Ensure that the installation of CUDA is in the CMAKE_PREFIX_PATH")
  endif()
else()
  if (cudnn)
    message(STATUS "Cannot select cudnn without selecting cuda or tmva-gpu. Option is ignored")
    set(cudnn OFF)
  endif()
endif()

##------------------------------------------------------------------------------------
# Check if we need libatomic to use atomic operations in the C++ code. On ARM systems
# we generally do. First just test if CMake is able to compile a test executable
# using atomic operations without the help of a library. Only if it can't do we start
# looking for libatomic for the build.
#
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <atomic>
#include <cstdint>
int main() {
   std::atomic<int> a1;
   int a1val = a1.load();
   (void)a1val;
   std::atomic<uint64_t> a2;
   uint64_t a2val = a2.load(std::memory_order_relaxed);
   (void)a2val;
   return 0;
}
" ROOT_HAVE_CXX_ATOMICS_WITHOUT_LIB)
set(ROOT_ATOMIC_LIBS)
if(NOT ROOT_HAVE_CXX_ATOMICS_WITHOUT_LIB)
  find_library(ROOT_ATOMIC_LIB NAMES atomic
    HINTS ENV LD_LIBRARY_PATH
    DOC "Path to the atomic library to use during the build")
  mark_as_advanced(ROOT_ATOMIC_LIB)
  if(ROOT_ATOMIC_LIB)
    set(ROOT_ATOMIC_LIBS ${ROOT_ATOMIC_LIB})
  endif()
endif()

# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT (global) core package
############################################################################

add_subdirectory(clib)
add_subdirectory(clingutils)
add_subdirectory(cont)
add_subdirectory(dictgen)
add_subdirectory(foundation)
add_subdirectory(meta)
add_subdirectory(metacling)
add_subdirectory(textinput)
add_subdirectory(thread)
add_subdirectory(zip)

add_subdirectory(base)

if(UNIX)
  add_subdirectory(unix)
  set(unix_objects $<TARGET_OBJECTS:Unix>)
endif()
if(WIN32)
  add_subdirectory(winnt)
  set(winnt_objects $<TARGET_OBJECTS:Winnt>)
endif()

#---G__CoreLegacy--------------------------------------------------------------

# libCoreLegacy is linked against zlib (based on the location returned by find_package or builtin_zlib). The dictionary of
# libCoreLegacy, G__CoreLegacy.cxx, should contain the include path to zlib.h in order to keep the symbol and header files in sync.
# The includePath variable of the dictionary is empty, but we do not notice it because, by chance, G__MathMore adds
# the same includePath for GSL. OTOH, if we have a small test case which works only with libCoreLegacy.so we never give a
# chance MathMore's dictionary to hide the problem and thus we pick up the system zlib.h.
#
# FIXME: Using include_directories is too intrusive as it does much more than what we want, that is, it will add this
# include path not only in the dictionary but as part of the compilation lines. It is harmless but suboptimal: we need a
# mechanism throough which we can express that we only need the path in the dictionary.
#
# NOTE: We cannot use set_property(TARGET G__CoreLegacy ...) becase this has to happen before registering the target.
include_directories(
  ${ZLIB_INCLUDE_DIR}
  base/inc
  clib/inc
  cont/inc
  foundation/inc
  unix/inc
  winnt/inc
  clingutils/inc
  meta/inc
  gui/inc
  textinput/inc
)

set(objectlibs $<TARGET_OBJECTS:Base>
               $<TARGET_OBJECTS:Clib>
               $<TARGET_OBJECTS:Cont>
               $<TARGET_OBJECTS:Foundation>
               $<TARGET_OBJECTS:Zip>
               $<TARGET_OBJECTS:Meta>
               $<TARGET_OBJECTS:TextInput>
               ${unix_objects}
               ${winnt_objects})

ROOT_OBJECT_LIBRARY(BaseTROOT ${CMAKE_SOURCE_DIR}/core/base/src/TROOT.cxx)
target_include_directories(BaseTROOT PRIVATE foundation/res)
#----------------------------------------------------------------------------------------

if(WIN32)
  set(corelinklibs shell32.lib WSock32.lib Oleaut32.lib Iphlpapi.lib)
elseif(APPLE)
  set(corelinklibs "-F/System/Library/PrivateFrameworks -framework CoreSymbolication")
endif()

add_subdirectory(rootcling_stage1)

#-------------------------------------------------------------------------------
ROOT_LINKER_LIBRARY(CoreLegacy $<TARGET_OBJECTS:BaseTROOT> ${objectlibs} BUILTINS)

ROOT_GENERATE_DICTIONARY(G__CoreLegacy
  ${CoreLegacy_dict_headers}
  ${Clib_dict_headers}
  ${Cont_dict_headers}
  ${Foundation_dict_headers}
  ${Macosx_dict_headers}
  ${Unix_dict_headers}
  ${Winnt_dict_headers}
  ${ClingUtils_dict_headers}
  ${Meta_dict_headers}
  ${TextInput_dict_headers}
  STAGE1
  MODULE
    CoreLegacy
  OPTIONS
  LINKDEF
    base/inc/LinkDef.h
)

# This is needed because LinkDef.h includes other LinkDef starting from ${CMAKE_SOURCE_DIR}
target_include_directories(CoreLegacy PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(CoreLegacy
  PRIVATE
    ${LIBLZMA_LIBRARIES}
   
    ZLIB::ZLIB
    ${ZSTD_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    ${corelinklibs}
)

add_dependencies(CoreLegacy CLING)

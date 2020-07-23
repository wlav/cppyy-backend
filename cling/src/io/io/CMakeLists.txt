# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT io/io package
############################################################################

ROOT_LINKER_LIBRARY(RIOLegacy
  src/TBufferFile.cxx
  src/TBufferIO.cxx
  src/TCollectionProxyFactory.cxx
  src/TContainerConverters.cxx
  src/TEmulatedMapProxy.cxx
  src/TEmulatedCollectionProxy.cxx
  src/TDirectoryFile.cxx
  src/TFree.cxx
  src/TFile.cxx
  src/TGenCollectionStreamer.cxx
  src/TGenCollectionProxy.cxx
  src/TKey.cxx
  src/TMemFile.cxx
  src/TStreamerInfo.cxx
  src/TStreamerInfoActions.cxx
  src/TStreamerInfoReadBuffer.cxx
  src/TStreamerInfoWriteBuffer.cxx
  $<TARGET_OBJECTS:RootPcmObjs>
  LIBRARIES
    ${CMAKE_DL_LIBS}
    ${ROOT_ATOMIC_LIBS}
  DEPENDENCIES
    CoreLegacy
    ThreadLegacy
)

target_include_directories(RIOLegacy PRIVATE ${CMAKE_SOURCE_DIR}/core/clib/res)

ROOT_GENERATE_DICTIONARY(G__RIOLegacy
  TBufferFile.h
  TBufferIO.h
  TCollectionProxyFactory.h
  TContainerConverters.h
  TEmulatedMapProxy.h
  TEmulatedCollectionProxy.h
  TDirectoryFile.h
  TFree.h
  TFile.h
  TGenCollectionStreamer.h
  TGenCollectionProxy.h
  TKey.h
  TMemFile.h
  TStreamerInfoActions.h
  TVirtualCollectionIterators.h
  TStreamerInfo.h
  TVirtualArray.h
  ${RIOLegacy_EXTRA_HEADERS}
  STAGE1
  MODULE
    RIOLegacy
  LINKDEF
    LinkDef.h
  OPTIONS
    -writeEmptyRootPCM
  DEPENDENCIES
    CoreLegacy
    ThreadLegacy
)

# TStreamerInfoReadBuffer in O0 needs 6k on the stack. It is called
# recursively, quickly exhausting the stack. Prevent that by forcing
# the many scope-local vars to share their stack space / become
# registers, thanks to the optimizer.
if(MSVC)
  set_source_files_properties(src/TStreamerInfoReadBuffer.cxx COMPILE_FLAGS "/O2")
else()
  set_source_files_properties(src/TStreamerInfoReadBuffer.cxx COMPILE_FLAGS "-O3")
endif()

ROOT_INSTALL_HEADERS()

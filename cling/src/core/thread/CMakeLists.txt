# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT core/thread package
############################################################################

if(WIN32)
  set(PLATFORM_FILTER FILTER "Posix")
  set(PLATFORM_HEADERS
    TWin32Condition.h
    TWin32Mutex.h
    TWin32Thread.h
    TWin32ThreadFactory.h
  )
else()
  set(PLATFORM_FILTER FILTER "Win32")
  set(PLATFORM_HEADERS
    TPosixCondition.h
    TPosixMutex.h
    TPosixThread.h
    TPosixThreadFactory.h
    PosixThreadInc.h
  )
endif()

ROOT_STANDARD_LIBRARY_PACKAGE(ThreadLegacy
  HEADERS
    ${PLATFORM_HEADERS}
    TCondition.h
    TConditionImp.h
    ThreadLocalStorage.h
    TMutex.h
    TMutexImp.h
    TThreadFactory.h
    TThread.h
    TThreadImp.h
    ROOT/TReentrantRWLock.hxx
    ROOT/TSpinMutex.hxx
  SOURCES
    src/TCondition.cxx
    src/TConditionImp.cxx
    src/TMutex.cxx
    src/TMutexImp.cxx
    src/TReentrantRWLock.cxx
    src/TRWMutexImp.cxx
    src/TThread.cxx
    src/TThreadFactory.cxx
    src/TThreadImp.cxx
  STAGE1
  DICTIONARY_OPTIONS
    -writeEmptyRootPCM 
  DEPENDENCIES
    CoreLegacy
  INSTALL_OPTIONS ${installoptions}
)

target_link_libraries(ThreadLegacy PUBLIC ${CMAKE_THREAD_LIBS_INIT})

if(WIN32)
  target_sources(ThreadLegacy PRIVATE
    src/TWin32Condition.cxx
    src/TWin32Mutex.cxx
    src/TWin32Thread.cxx
    src/TWin32ThreadFactory.cxx
  )
else()
  target_sources(ThreadLegacy PRIVATE
    src/TPosixCondition.cxx
    src/TPosixMutex.cxx
    src/TPosixThread.cxx
    src/TPosixThreadFactory.cxx
  )
endif()

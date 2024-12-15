# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

set(ROOT_PLATFORM win32)

#----Check the compiler that is used-----------------------------------------------------
if(CMAKE_COMPILER_IS_GNUCXX)

  set(ROOT_ARCHITECTURE win32gcc)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pipe  -Wall -W -Woverloaded-virtual")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pipe -Wall -W")
  set(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -std=legacy")

  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

  # Select flags.
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE        "-O2 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG          "-g")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-O2 -g -DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE          "-O2 -DNDEBUG")
  set(CMAKE_C_FLAGS_DEBUG            "-g")

  #---Set Linker flags----------------------------------------------------------------------
  set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS}")
  set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS}")
elseif(MSVC)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
     set(WIN_EXTRA_DEFS "-D_WINDOWS -DWIN32 -D_AMD64_")
     set(MACHINE_ARCH X64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
     set(WIN_EXTRA_DEFS "-D_WINDOWS -DWIN32 -D_X86_")
     set(MACHINE_ARCH X86)
  endif()

  set(ROOT_ARCHITECTURE win32)

  math(EXPR VC_MAJOR "${MSVC_VERSION} / 100")
  math(EXPR VC_MINOR "${MSVC_VERSION} % 100")

  if(winrtdebug)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
      string(REGEX MATCH "-D_ITERATOR_DEBUG_LEVEL=0" result ${CMAKE_CXX_FLAGS_DEBUG})
      if(NOT result MATCHES "-D_ITERATOR_DEBUG_LEVEL=0")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_ITERATOR_DEBUG_LEVEL=0")
      endif()
    endif()
    set(BLDCXXFLAGS "-MDd")
    set(BLDCFLAGS "-MDd")
  else()
    set(BLDCXXFLAGS "-MD")
    set(BLDCFLAGS "-MD")
    string(REPLACE "-MDd" "-MD" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "-MDd" "-MD" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
  endif()

  if(CMAKE_PROJECT_NAME STREQUAL ROOT)
    set(CMAKE_CXX_FLAGS "-nologo -I${CMAKE_SOURCE_DIR}/build/win -FIw32pragma.h -FIsehmap.h ${BLDCXXFLAGS} ${WIN_EXTRA_DEFS} -EHsc- -W3 -wd4141 -wd4291 -wd4244 -wd4049 -wd4146 -D_WIN32 ${ARCH} -D_XKEYCHECK_H -D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS")
    set(CMAKE_C_FLAGS   "-nologo -I${CMAKE_SOURCE_DIR}/build/win -FIw32pragma.h -FIsehmap.h ${BLDCFLAGS} ${WIN_EXTRA_DEFS} -EHsc- -W3 -D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER -DNOMINMAX")
    install(FILES ${CMAKE_SOURCE_DIR}/build/win/w32pragma.h  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT headers)
    install(FILES ${CMAKE_SOURCE_DIR}/build/win/sehmap.h  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT headers)
  else()
    set(CMAKE_CXX_FLAGS "-nologo -FIw32pragma.h -FIsehmap.h ${BLDCXXFLAGS} ${WIN_EXTRA_DEFS} -EHsc- -W3 -wd4244")
    set(CMAKE_C_FLAGS   "-nologo -FIw32pragma.h -FIsehmap.h ${BLDCFLAGS} ${WIN_EXTRA_DEFS} -EHsc- -W3")
  endif()

  #---Select compiler flags----------------------------------------------------------------
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-MD -O2 -Ob1 -Z7 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_MINSIZEREL     "-MD -O1 -Ob1 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE        "-MD -O2 -Ob2 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG          "-MDd -Od -Ob0 -Z7")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-MD -O2 -Ob1 -Z7 -DNDEBUG")
  set(CMAKE_C_FLAGS_MINSIZEREL       "-MD -O1 -Ob1 -DNDEBUG")
  set(CMAKE_C_FLAGS_RELEASE          "-MD -O2 -Ob2 -DNDEBUG")
  set(CMAKE_C_FLAGS_DEBUG            "-MDd -Od -Ob0 -Z7")

  #---Set Linker flags----------------------------------------------------------------------
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -ignore:4049,4206,4217,4221 -incremental:no")
  set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -ignore:4049,4206,4217,4221 -incremental:no")

  foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} )
    set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
    set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY} )
  endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )
else()
  message(FATAL_ERROR "There is no setup for compiler '${CMAKE_CXX_COMPILER}' on this Windows system up to now. Stop cmake at this point.")
endif()

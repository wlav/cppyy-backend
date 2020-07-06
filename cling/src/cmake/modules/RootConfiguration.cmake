# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

INCLUDE (CheckCXXSourceCompiles)

#---Define a function to do not polute the top level namespace with unneeded variables-----------------------
function(RootConfigure)

#---Define all sort of variables to bridge between the old Module.mk and the new CMake equivalents-----------
foreach(v 1 ON YES TRUE Y on yes true y)
  set(value${v} yes)
endforeach()
foreach(v 0 OFF NO FALSE N IGNORE off no false n ignore)
  set(value${v} no)
endforeach()

set(ROOT_DICTTYPE cint)
#set(ROOT_CONFIGARGS "")
set(top_srcdir ${CMAKE_SOURCE_DIR})
set(top_builddir ${CMAKE_BINARY_DIR})
set(architecture ${ROOT_ARCHITECTURE})
set(platform ${ROOT_PLATFORM})
set(host)
set(useconfig FALSE)
set(major ${ROOT_MAJOR_VERSION})
set(minor ${ROOT_MINOR_VERSION})
set(revis ${ROOT_PATCH_VERSION})
set(mkliboption "-v ${major} ${minor} ${revis} ")
set(cflags ${CMAKE_CXX_FLAGS})
set(ldflags ${CMAKE_CXX_LINK_FLAGS})

set(winrtdebug ${value${winrtdebug}})
set(exceptions ${value${exceptions}})

if(gnuinstall)
  set(prefix ${CMAKE_INSTALL_PREFIX})
else()
  set(prefix $(ROOTSYS))
endif()
if(IS_ABSOLUTE ${CMAKE_INSTALL_SYSCONFDIR})
  set(etcdir ${CMAKE_INSTALL_SYSCONFDIR})
else()
  set(etcdir ${prefix}/${CMAKE_INSTALL_SYSCONFDIR})
endif()
if(IS_ABSOLUTE ${CMAKE_INSTALL_BINDIR})
  set(bindir ${CMAKE_INSTALL_BINDIR})
else()
  set(bindir ${prefix}/${CMAKE_INSTALL_BINDIR})
endif()
if(IS_ABSOLUTE ${CMAKE_INSTALL_LIBDIR})
  set(libdir ${CMAKE_INSTALL_LIBDIR})
else()
  set(libdir ${prefix}/${CMAKE_INSTALL_LIBDIR})
endif()
if(IS_ABSOLUTE ${CMAKE_INSTALL_INCLUDEDIR})
  set(incdir ${CMAKE_INSTALL_INCLUDEDIR})
else()
  set(incdir ${prefix}/${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(IS_ABSOLUTE ${CMAKE_INSTALL_SRCDIR})
  set(srcdir ${CMAKE_INSTALL_SRCDIR})
else()
  set(srcdir ${prefix}/${CMAKE_INSTALL_SRCDIR})
endif()

set(thread yes)
set(enable_thread yes)
set(threadflag ${CMAKE_THREAD_FLAG})
set(threadlibdir)
set(threadlib ${CMAKE_THREAD_LIBS_INIT})

set(builtinzlib ${value${builtin_zlib}})
set(zliblibdir ${ZLIB_LIBRARY_DIR})
set(zliblib ${ZLIB_LIBRARY})
set(zlibincdir ${ZLIB_INCLUDE_DIR})

set(shadowpw ${value${shadowpw}})

set(cursesincdir ${CURSES_INCLUDE_DIR})
set(curseslibdir)
set(curseslib ${CURSES_LIBRARIES})
set(curseshdr ${CURSES_HEADER_FILE})
set(buildeditline ${value${editline}})
set(dicttype ${ROOT_DICTTYPE})

find_program(PERL_EXECUTABLE perl)
set(perl ${PERL_EXECUTABLE})


#---RConfigure-------------------------------------------------------------------------------------------------
# set(setresuid undef)
CHECK_CXX_SOURCE_COMPILES("#include <unistd.h>
  int main() { uid_t r = 0, e = 0, s = 0; if (setresuid(r, e, s) != 0) { }; return 0;}" found_setresuid)
if(found_setresuid)
  set(setresuid define)
else()
  set(setresuid undef)
endif()

if(mathmore)
  set(hasmathmore define)
else()
  set(hasmathmore undef)
endif()
if(CMAKE_USE_PTHREADS_INIT)
  set(haspthread define)
else()
  set(haspthread undef)
endif()

set(usezlib undef)
set(use${compression_default} define)

# cloudflare zlib is available only on x86 and aarch64 platforms with Linux
# for other platforms we have available builtin zlib 1.2.8
if(ZLIB_CF)
  set(usecloudflarezlib define)
else()
  set(usecloudflarezlib undef)
endif()
if(runtime_cxxmodules)
  set(usecxxmodules define)
else()
  set(usecxxmodules undef)
endif()
if(libcxx)
  set(uselibc++ define)
else()
  set(uselibc++ undef)
endif()
if(gcctoolchain)
  set(setgcctoolchain define)
else()
  set(setgcctoolchain undef)
endif()

# clear cache to allow reconfiguring
# with a different CMAKE_CXX_STANDARD
unset(found_stdapply CACHE)
unset(found_stdindexsequence CACHE)
unset(found_stdinvoke CACHE)
unset(found_stdstringview CACHE)
unset(found_stdexpstringview CACHE)
unset(found_stod_stringview CACHE)

set(hasstdexpstringview undef)
CHECK_CXX_SOURCE_COMPILES("#include <string_view>
  int main() { char arr[3] = {'B', 'a', 'r'}; std::string_view strv(arr, sizeof(arr)); return 0;}" found_stdstringview)
if(found_stdstringview)
  set(hasstdstringview define)
else()
  set(hasstdstringview undef)

  CHECK_CXX_SOURCE_COMPILES("#include <experimental/string_view>
   int main() { char arr[3] = {'B', 'a', 'r'}; std::experimental::string_view strv(arr, sizeof(arr)); return 0;}" found_stdexpstringview)
  if(found_stdexpstringview)
    set(hasstdexpstringview define)
  else()
    set(hasstdexpstringview undef)
  endif()
endif()

if(found_stdstringview)
  CHECK_CXX_SOURCE_COMPILES("#include <string_view>
     int main() { size_t pos; std::string_view str; std::stod(str,&pos); return 0;}" found_stod_stringview)
elseif(found_stdexpstringview)
  CHECK_CXX_SOURCE_COMPILES("#include <experimental/string_view>
     int main() { size_t pos; std::experimental::string_view str; std::stod(str,&pos); return 0;}" found_stod_stringview)
else()
  set(found_stod_stringview false)
endif()

if(found_stod_stringview)
  set(hasstodstringview define)
else()
  set(hasstodstringview undef)
endif()

if(found_stdstringview)
  CHECK_CXX_SOURCE_COMPILES("#include <string>
     #include <string_view>
     int main() { std::string s; std::string_view v; s += v; return 0;}" found_opplusequal_stringview)
elseif(found_stdexpstringview)
  CHECK_CXX_SOURCE_COMPILES("#include <string>
     #include <experimental/string_view>
     int main() { std::string s; std::experimental::string_view v; s += v; return 0;}" found_opplusequal_stringview)
else()
  set(found_opplusequal_stringview false)
endif()

if(found_opplusequal_stringview)
  set(hasopplusequalstringview define)
else()
  set(hasopplusequalstringview undef)
endif()

CHECK_CXX_SOURCE_COMPILES("#include <tuple>
int main() { std::apply([](int, int){}, std::make_tuple(1,2)); return 0;}" found_stdapply)
if(found_stdapply)
  set(hasstdapply define)
else()
  set(hasstdapply undef)
endif()

CHECK_CXX_SOURCE_COMPILES("#include <functional>
int main() { return std::invoke([](int i){return i;}, 0); }" found_stdinvoke)
if(found_stdinvoke)
  set(hasstdinvoke define)
else()
  set(hasstdinvoke undef)
endif()

CHECK_CXX_SOURCE_COMPILES("#include <utility>
#include <type_traits>
int main() {
  static_assert(std::is_same<std::integer_sequence<std::size_t, 0, 1, 2>, std::make_index_sequence<3>>::value, \"\");
  return 0;
}" found_stdindexsequence)
if(found_stdindexsequence)
  set(hasstdindexsequence define)
else()
  set(hasstdindexsequence undef)
endif()

CHECK_CXX_SOURCE_COMPILES("
inline __attribute__((always_inline)) bool TestBit(unsigned long f) { return f != 0; };
int main() { return TestBit(0); }" found_attribute_always_inline)
if(found_attribute_always_inline)
   set(has_found_attribute_always_inline define)
else()
   set(has_found_attribute_always_inline undef)
endif()

CHECK_CXX_SOURCE_COMPILES("
inline __attribute__((noinline)) bool TestBit(unsigned long f) { return f != 0; };
int main() { return TestBit(0); }" has_found_attribute_noinline)
if(has_found_attribute_noinline)
   set(has_found_attribute_noinline define)
else()
   set(has_found_attribute_noinline undef)
endif()

#---root-config----------------------------------------------------------------------------------------------
ROOT_GET_OPTIONS(features ENABLED)
set(features "cxx${CMAKE_CXX_STANDARD} ${features}")
set(configfeatures ${features})
set(configargs ${ROOT_CONFIGARGS})
set(configoptions ${ROOT_CONFIGARGS})
set(configstd ${CMAKE_CXX${CMAKE_CXX_STANDARD}_STANDARD_COMPILE_OPTION})
get_filename_component(altcc ${CMAKE_C_COMPILER} NAME)
get_filename_component(altcxx ${CMAKE_CXX_COMPILER} NAME)
get_filename_component(altf77 "${CMAKE_Fortran_COMPILER}" NAME)
get_filename_component(altld ${CMAKE_CXX_COMPILER} NAME)

set(pythonvers ${PYTHON_VERSION_STRING})

#---RConfigure.h---------------------------------------------------------------------------------------------
configure_file(${PROJECT_SOURCE_DIR}/config/RConfigure.in include/RConfigure.h NEWLINE_STYLE UNIX)
install(FILES ${CMAKE_BINARY_DIR}/include/RConfigure.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#---Configure and install various files----------------------------------------------------------------------
execute_Process(COMMAND hostname OUTPUT_VARIABLE BuildNodeInfo OUTPUT_STRIP_TRAILING_WHITESPACE )

configure_file(${CMAKE_SOURCE_DIR}/config/rootrc.in ${CMAKE_BINARY_DIR}/etc/system.rootrc @ONLY NEWLINE_STYLE UNIX)
configure_file(${CMAKE_SOURCE_DIR}/config/rootauthrc.in ${CMAKE_BINARY_DIR}/etc/system.rootauthrc @ONLY NEWLINE_STYLE UNIX)

configure_file(${CMAKE_SOURCE_DIR}/config/RConfigOptions.in include/RConfigOptions.h NEWLINE_STYLE UNIX)

configure_file(${CMAKE_SOURCE_DIR}/config/Makefile-comp.in config/Makefile.comp NEWLINE_STYLE UNIX)
configure_file(${CMAKE_SOURCE_DIR}/config/Makefile.in config/Makefile.config NEWLINE_STYLE UNIX)

#---Generate the ROOTConfig files to be used by CMake projects-----------------------------------------------
ROOT_GET_OPTIONS(ROOT_ALL_OPTIONS)
ROOT_GET_OPTIONS(ROOT_ENABLED_OPTIONS ENABLED)
configure_file(${CMAKE_SOURCE_DIR}/cmake/scripts/ROOTConfig-version.cmake.in
               ${CMAKE_BINARY_DIR}/ROOTConfig-version.cmake @ONLY NEWLINE_STYLE UNIX)

#---Compiler flags (because user apps are a bit dependent on them...)----------------------------------------
string(REGEX REPLACE "(^|[ ]*)-W[^ ]*" "" __cxxflags "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "(^|[ ]*)-W[^ ]*" "" __cflags "${CMAKE_C_FLAGS}")

if (cxxmodules)
  # Re-add the -Wno-module-import-in-extern-c which we just filtered out.
  # We want it because it changes the module cache hash and causes modules to be
  # rebuilt.
  # FIXME: We should review how we do the regex.
  set(ROOT_CXX_FLAGS "${ROOT_CXX_FLAGS} -Wno-module-import-in-extern-c")
  set(ROOT_C_FLAGS "${ROOT_C_FLAGS} -Wno-module-import-in-extern-c")
endif()

string(REGEX REPLACE "(^|[ ]*)-W[^ ]*" "" __fflags "${CMAKE_Fortran_FLAGS}")
string(REGEX MATCHALL "-(D|U)[^ ]*" __defs "${CMAKE_CXX_FLAGS}")
set(ROOT_COMPILER_FLAG_HINTS "#
set(ROOT_DEFINITIONS \"${__defs}\")
set(ROOT_CXX_FLAGS \"${__cxxflags}\")
set(ROOT_C_FLAGS \"${__cflags}\")
set(ROOT_fortran_FLAGS \"${__fflags}\")
set(ROOT_EXE_LINKER_FLAGS \"${CMAKE_EXE_LINKER_FLAGS}\")")
set(ROOT_BINDIR ${CMAKE_BINARY_DIR}/bin CACHE INTERNAL "")

#---To be used from the binary tree--------------------------------------------------------------------------
set(ROOT_INCLUDE_DIR_SETUP "
# ROOT configured for use from the build tree - absolute paths are used.
set(ROOT_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/include)
")
set(ROOT_LIBRARY_DIR_SETUP "
# ROOT configured for use from the build tree - absolute paths are used.
set(ROOT_LIBRARY_DIR ${CMAKE_BINARY_DIR}/lib)
")
set(ROOT_BINDIR_SETUP "
# ROOT configured for use from the build tree - absolute paths are used.
set(ROOT_BINDIR ${CMAKE_BINARY_DIR}/bin)
")
# Deprecated value ROOT_BINARY_DIR
set(ROOT_BINARY_DIR_SETUP "
# Deprecated value, please don't use it and use ROOT_BINDIR instead.
set(ROOT_BINARY_DIR ${ROOT_BINDIR})
")

get_property(exported_targets GLOBAL PROPERTY ROOT_EXPORTED_TARGETS)
export(TARGETS ${exported_targets} NAMESPACE ROOT:: FILE ${PROJECT_BINARY_DIR}/ROOTConfig-targets.cmake)
configure_file(${CMAKE_SOURCE_DIR}/cmake/scripts/ROOTConfig.cmake.in
               ${CMAKE_BINARY_DIR}/ROOTConfig.cmake @ONLY NEWLINE_STYLE UNIX)
configure_file(${CMAKE_SOURCE_DIR}/cmake/scripts/RootUseFile.cmake.in
               ${CMAKE_BINARY_DIR}/ROOTUseFile.cmake @ONLY NEWLINE_STYLE UNIX)

#---To be used from the install tree--------------------------------------------------------------------------
# Need to calculate actual relative paths from CMAKEDIR to other locations
file(RELATIVE_PATH ROOT_CMAKE_TO_INCLUDE_DIR "${CMAKE_INSTALL_FULL_CMAKEDIR}" "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
file(RELATIVE_PATH ROOT_CMAKE_TO_LIB_DIR "${CMAKE_INSTALL_FULL_CMAKEDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
file(RELATIVE_PATH ROOT_CMAKE_TO_BIN_DIR "${CMAKE_INSTALL_FULL_CMAKEDIR}" "${CMAKE_INSTALL_FULL_BINDIR}")

set(ROOT_INCLUDE_DIR_SETUP "
# ROOT configured for the install with relative paths, so use these
get_filename_component(ROOT_INCLUDE_DIRS \"\${_thisdir}/${ROOT_CMAKE_TO_INCLUDE_DIR}\" ABSOLUTE)
")
set(ROOT_LIBRARY_DIR_SETUP "
# ROOT configured for the install with relative paths, so use these
get_filename_component(ROOT_LIBRARY_DIR \"\${_thisdir}/${ROOT_CMAKE_TO_LIB_DIR}\" ABSOLUTE)
")
set(ROOT_BINDIR_SETUP "
# ROOT configured for the install with relative paths, so use these
get_filename_component(ROOT_BINDIR \"\${_thisdir}/${ROOT_CMAKE_TO_BIN_DIR}\" ABSOLUTE)
")
# Deprecated value ROOT_BINARY_DIR
set(ROOT_BINARY_DIR_SETUP "
# Deprecated value, please don't use it and use ROOT_BINDIR instead.
get_filename_component(ROOT_BINARY_DIR \"\${ROOT_BINDIR}\" ABSOLUTE)
")

# used by ROOTConfig.cmake from the build directory
configure_file(${CMAKE_SOURCE_DIR}/cmake/modules/RootMacros.cmake
               ${CMAKE_BINARY_DIR}/RootMacros.cmake COPYONLY)

# used by roottest to run tests against ROOT build
configure_file(${CMAKE_SOURCE_DIR}/cmake/scripts/ROOTConfig.cmake.in
               ${CMAKE_BINARY_DIR}/installtree/ROOTConfig.cmake @ONLY NEWLINE_STYLE UNIX)
configure_file(${CMAKE_SOURCE_DIR}/cmake/scripts/RootUseFile.cmake.in
               ${CMAKE_BINARY_DIR}/installtree/ROOTUseFile.cmake @ONLY NEWLINE_STYLE UNIX)
install(FILES ${CMAKE_BINARY_DIR}/ROOTConfig-version.cmake
              ${CMAKE_BINARY_DIR}/installtree/ROOTUseFile.cmake
              ${CMAKE_BINARY_DIR}/installtree/ROOTConfig.cmake DESTINATION ${CMAKE_INSTALL_CMAKEDIR})
install(EXPORT ${CMAKE_PROJECT_NAME}Exports NAMESPACE ROOT:: FILE ROOTConfig-targets.cmake DESTINATION ${CMAKE_INSTALL_CMAKEDIR})


#---Especial definitions for root-config et al.--------------------------------------------------------------
if(prefix STREQUAL "$(ROOTSYS)")
  foreach(d prefix bindir libdir incdir etcdir)
    string(REPLACE "$(ROOTSYS)" "$ROOTSYS"  ${d} ${${d}})
  endforeach()
endif()


#---compiledata.h--------------------------------------------------------------------------------------------

if(APPLE AND runtime_cxxmodules)
  # Modules have superior dynamic linker and they can resolve undefined symbols upon library loading.
  set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS} -undefined dynamic_lookup")
endif()

if(WIN32)
  # We cannot use the compiledata.sh script for windows
  configure_file(${CMAKE_SOURCE_DIR}/cmake/scripts/compiledata.win32.in ${CMAKE_BINARY_DIR}/include/compiledata.h NEWLINE_STYLE UNIX)
else()
  execute_process(COMMAND ${CMAKE_SOURCE_DIR}/build/unix/compiledata.sh
    ${CMAKE_BINARY_DIR}/include/compiledata.h "${CMAKE_CXX_COMPILER}"
        "${CMAKE_CXX_FLAGS_RELEASE}" "${CMAKE_CXX_FLAGS_DEBUG}" "${CMAKE_CXX_FLAGS}"
        "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS}" "${CMAKE_EXE_FLAGS}" "so"
        "${libdir}" "-lCoreLegacy" "${incdir}" "" "" "${ROOT_ARCHITECTURE}" "")
endif()

#---Get the value of CMAKE_CXX_FLAGS provided by the user in the command line
set(usercflags ${CMAKE_CXX_FLAGS-CACHED})
file(REMOVE ${CMAKE_BINARY_DIR}/installtree/root-config)
configure_file(${CMAKE_SOURCE_DIR}/config/root-config.in ${CMAKE_BINARY_DIR}/installtree/root-config @ONLY NEWLINE_STYLE UNIX)
configure_file(${CMAKE_SOURCE_DIR}/config/thisroot.sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/thisroot.sh @ONLY NEWLINE_STYLE UNIX)
configure_file(${CMAKE_SOURCE_DIR}/config/thisroot.csh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/thisroot.csh @ONLY NEWLINE_STYLE UNIX)
if(WIN32)
  set(thisrootbat ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/thisroot.bat)
  configure_file(${CMAKE_SOURCE_DIR}/config/thisroot.bat ${thisrootbat} @ONLY)
endif()

#--Local root-configure
set(prefix $ROOTSYS)
set(bindir $ROOTSYS/bin)
set(libdir $ROOTSYS/lib)
set(incdir $ROOTSYS/include)
set(etcdir $ROOTSYS/etc)
configure_file(${CMAKE_SOURCE_DIR}/config/root-config.in ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/root-config @ONLY NEWLINE_STYLE UNIX)

install(FILES ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/thisroot.sh
              ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/thisroot.csh
              ${thisrootbat}
              PERMISSIONS OWNER_WRITE OWNER_READ
                          GROUP_READ
                          WORLD_READ
              DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_BINARY_DIR}/installtree/root-config
              PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                          GROUP_EXECUTE GROUP_READ
                          WORLD_EXECUTE WORLD_READ
              DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${CMAKE_BINARY_DIR}/include/RConfigOptions.h
              ${CMAKE_BINARY_DIR}/include/compiledata.h
              DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${CMAKE_BINARY_DIR}/etc/system.rootrc
              ${CMAKE_BINARY_DIR}/etc/system.rootauthrc
              DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})

if(NOT gnuinstall)
  install(FILES ${CMAKE_BINARY_DIR}/config/Makefile.comp
                ${CMAKE_BINARY_DIR}/config/Makefile.config
                DESTINATION config)
endif()


endfunction()
RootConfigure()

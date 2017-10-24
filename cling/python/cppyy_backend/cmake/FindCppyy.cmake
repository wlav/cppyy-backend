#.rst:
# FindCppyy
# -------
#
# Find Cppyy
#
# This module finds an installed Cppyy.  It sets the following variables:
#
# ::
#
#   Cppyy_FOUND - set to true if Cppyy is found
#   Cppyy_DIR - the directory where Cppyy is installed
#   Cppyy_EXECUTABLE - the path to the Cppyy executable
#   CppyyConfig_EXECUTABLE - the path to the Cppyy config executable
#   Cppyy_INCLUDE_DIRS - Where to find the ROOT header files.
#   Cppyy_VERSION - the version number of the Cppyy backend.
#
#
# The minimum required version of Cppyy can be specified using the
# standard syntax, e.g.  find_package(Cppyy 4.19)
#
# All information is collected from the CppyyConfig_EXECUTABLE so the
# version to be found can be changed from the command line by means of
# setting CppyyConfig_EXECUTABLE.

find_program(Cppyy_EXECUTABLE NAMES rootcling)
find_program(CppyyConfig_EXECUTABLE NAMES cling-config)

if(Cppyy_EXECUTABLE AND CppyyConfig_EXECUTABLE)
  #
  # Cppyy_DIR.
  #
  execute_process(
    COMMAND ${CppyyConfig_EXECUTABLE} --cmake
    RESULT_VARIABLE _STATUS
    OUTPUT_VARIABLE _STDOUT
    ERROR_VARIABLE _STDERR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(Cppyy_DIR ${_STDOUT})
  #
  # Cppyy_INCLUDE_DIRS.
  #
  execute_process(
    COMMAND ${CppyyConfig_EXECUTABLE} --cppflags
    RESULT_VARIABLE _STATUS
    OUTPUT_VARIABLE _STDOUT
    ERROR_VARIABLE _STDERR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX REPLACE "^-I" "" _STDOUT "${_STDOUT}")
  set(Cppyy_INCLUDE_DIRS ${_STDOUT})
  find_package(ROOT QUIET REQUIRED PATHS ${CMAKE_CURRENT_LIST_DIR})
  if(ROOT_FOUND)
    #
    # Cppyy_VERSION.
    #
    set(Cppyy_VERSION ${ROOT_VERSION})
  endif()
endif()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
    Cppyy
    REQUIRED_VARS Cppyy_EXECUTABLE CppyyConfig_EXECUTABLE Cppyy_DIR Cppyy_INCLUDE_DIRS
    VERSION_VAR Cppyy_VERSION)

mark_as_advanced(Cppyy_VERSION)

#
# Generate a set of bindings from a set of C/C++ header files.
#
#   CPPYY_ADD_BINDINGS(
#       pkg_lib
#       [LANGUAGE language]
#       [LINKDEFS linkdef...]
#       [IMPORTS pcm...]
#       [OPTIONS option...]
#       [CLING_OPTIONS option...]
#       [COMPILE_OPTIONS option...]
#       [LINK_LIBRARIES library...]
#       [H_DIR h_directory]
#       H_FILES h_file...)
#
# Arguments and options:
#
#   pkg_lib             The name of the library to generate.
#
#                       TODO: The name does not contribute to the Python
#                       namespace.
#
#   LANGUAGE language   "C++" or "C". Default is "C++".
#
#   LINKDEFS linkdefs   Files which contain additional manual definitions
#                       for the linkdef.h file used by rootcling. See
#                       https://root.cern.ch/root/html/guides/users-guide/AddingaClass.html#the-linkdef.h-file.
#
#                       TODO: For now, any private definitions in H_FILES
#                       must be disabled using something like
#                       "#pragma link off struct KDcrawIface::KDcraw::QPrivateSignal;".
#
#   IMPORTS pcm         Files which contain previously-generated bindings
#                       which pkg_lib depends on.
#
#   OPTIONS option      Options which are to be passed into both rootcling
#                       and compile/link commands.
#
#   CLING_OPTIONS option
#                       Options which are to be passed into the rootcling
#                       command. For example, bindings which depend on Qt
#                       may need "-D__PIC__;-Wno-macro-redefined" as per
#                       https://sft.its.cern.ch/jira/browse/ROOT-8719.
#
#   COMPILE_OPTIONS option
#                       Options which are to be passed into the compile/link
#                       command.
#
#   LINK_LIBRARIES library
#                       Libraries to link against.
#
#   H_DIR h_directory   Base directory for H_FILES.
#
#   H_FILES h_file      Header files for which to generate bindings in pkg_lib.
#                       Absolute filenames, or filenames relative to H_DIR. All
#                       definitions found directly in these files will contribute
#                       to the bindings. (NOTE: This means that if "forwarding
#                       headers" are present, the real "legacy" headers must be
#                       specified as H_FILES).
#
# Examples:
#
#   set(QT5 "/usr/include/x86_64-linux-gnu/qt5")
#   set(KF5 "/usr/include/KF5")
#   CPPYY_ADD_BINDINGS(
#       "KDCRAW"
#       LANGUAGE "C++"
#       LANGUAGE_STANDARD "14"
#       LINKDEFS "../linkdef_overrides.h"
#       OPTIONS "-I${QT5};-I${QT5}/QtCore"
#       CLING_OPTIONS "-D__PIC__;-Wno-macro-redefined"
#       LINK_LIBRARIES "KF5KDcraw"
#       H_DIR "${KF5}/KDCRAW"
#       H_FILES "kdcraw/dcrawinfocontainer.h;kdcraw/kdcraw.h;kdcraw/rawdecodingsettings.h;kdcraw/rawfiles.h")
#
function(CPPYY_ADD_BINDINGS pkg_lib)
  include(${ROOT_USE_FILE})
  cmake_parse_arguments(
    ARG
    ""
    "LANGUAGE;LANGUAGE_STANDARD;H_DIR"
    "LINKDEFS;IMPORTS;OPTIONS;CLING_OPTIONS;COMPILE_OPTIONS;LINK_LIBRARIES;H_FILES"
    ${ARGN})
  set(pkg_file ${CMAKE_SHARED_LIBRARY_PREFIX}${pkg_lib}${CMAKE_SHARED_LIBRARY_SUFFIX})
  #
  # Language and language standard.
  #
  if("${ARG_LANGUAGE}" STREQUAL "")
    set(ARG_LANGUAGE "C++")
  endif()
  if("${ARG_LANGUAGE}" STREQUAL "C++")
    if("${ARG_LANGUAGE_STANDARD}" STREQUAL "")
      set(ARG_LANGUAGE_STANDARD "14")
    endif()
  elseif("${ARG_LANGUAGE}" STREQUAL "C")
    if("${ARG_LANGUAGE_STANDARD}" STREQUAL "")
      set(ARG_LANGUAGE_STANDARD "11")
    endif()
  else()
    message(SEND_ERROR "LANGUAGE must be 'C' or 'C++'")
  endif()
  #
  # Make H_FILES with absolute paths.
  #
  if("${ARG_H_FILES}" STREQUAL "")
    message(SEND_ERROR "No H_FILES specified")
  endif()
  if("${ARG_H_DIR}" STREQUAL "")
    set(ARG_H_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  set(tmp)
  foreach(h_file IN LISTS ARG_H_FILES)
    if(NOT IS_ABSOLUTE ${h_file})
      set(h_file ${ARG_H_DIR}/${h_file})
    endif()
    if(NOT EXISTS ${h_file})
      message(WARNING "H_FILES ${h_file} does not exist")
    endif()
    list(APPEND tmp ${h_file})
  endforeach(h_file)
  set(ARG_H_FILES ${tmp})
  #
  # Create the linkdef.h file with a line for each h_file.
  #
  set(out_linkdef ${CMAKE_CURRENT_BINARY_DIR}/linkdef.h)
  file(WRITE ${out_linkdef} "/* Per H_FILES entries: */\n")
  foreach(h_file IN LISTS ARG_H_FILES)
    file(APPEND ${out_linkdef} "#pragma link ${ARG_LANGUAGE} defined_in ${h_file};\n")
  endforeach(h_file)
  #
  # Append any manually-provided linkdef.h content.
  #
  if(NOT "${ARG_LINKDEFS}" STREQUAL "")
    foreach(in_linkdef IN LISTS ARG_LINKDEFS)
      if(NOT IS_ABSOLUTE ${in_linkdef})
        set(in_linkdef ${CMAKE_CURRENT_SOURCE_DIR}/${in_linkdef})
      endif()
      if(NOT EXISTS ${in_linkdef})
        message(WARNING "LINKDEFS ${in_linkdef} does not exist")
      endif()
      file(APPEND ${out_linkdef} "/* Copied from ${in_linkdef}: */\n")
      file(STRINGS ${in_linkdef} in_linkdef NEWLINE_CONSUME)
      file(APPEND ${out_linkdef} ${in_linkdef})
    endforeach(in_linkdef)
  endif()
  #
  # Set up arguments for rootcling.
  #
  set(cling_args)
  list(APPEND cling_args "-f" ${CMAKE_CURRENT_BINARY_DIR}/${pkg_lib}.cpp)
  list(APPEND cling_args "-s" ${pkg_lib})
  foreach(in_pcm IN LISTS ARG_IMPORTS)
    #
    # Create -m options for any imported .pcm files.
    #
    list(APPEND cling_args "-m" "${in_pcm}")
  endforeach(in_pcm)
  list(APPEND cling_args "-rmf" ${pkg_lib}.rootmap "-rml" ${pkg_file})
  list(APPEND cling_args "-I${ARG_H_DIR}")
  if("${ARG_LANGUAGE}" STREQUAL "C++")
    list(APPEND cling_args "-std=c++${ARG_LANGUAGE_STANDARD}")
  elseif("${ARG_LANGUAGE}" STREQUAL "C")
    list(APPEND cling_args "-std=c${ARG_LANGUAGE_STANDARD}")
  endif()
  list(APPEND cling_args "${ARG_OPTIONS}")
  list(APPEND cling_args "${ARG_CLING_OPTIONS}")
  list(APPEND cling_args "${ARG_H_FILES}")
  list(APPEND cling_args ${out_linkdef})
  #
  # Run rootcling, specifying the generated output.
  #
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${pkg_lib}.cpp
    COMMAND ${Cppyy_EXECUTABLE} ${cling_args})
  #
  # Compile/link.
  #
  add_library(${pkg_lib} SHARED ${CMAKE_CURRENT_BINARY_DIR}/${pkg_lib}.cpp)
  if("${ARG_LANGUAGE}" STREQUAL "C++")
    set_property(TARGET ${pkg_lib} PROPERTY CXX_STANDARD ${ARG_LANGUAGE_STANDARD})
  elseif("${ARG_LANGUAGE}" STREQUAL "C")
    set_property(TARGET ${pkg_lib} PROPERTY C_STANDARD ${ARG_LANGUAGE_STANDARD})
  endif()
  target_include_directories(${pkg_lib} PRIVATE ${Cppyy_INCLUDE_DIRS} ${ARG_H_DIR})
  target_compile_options(${pkg_lib} PRIVATE ${ARG_OPTIONS} ${ARG_COMPILE_OPTIONS})
  target_link_libraries(${pkg_lib} ${ARG_LINK_LIBRARIES})
endfunction(CPPYY_ADD_BINDINGS)

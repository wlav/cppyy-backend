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
#   Cppyy_INCLUDE_DIRS - Where to find the ROOT header files.
#   Cppyy_VERSION - the version number of the Cppyy backend.
#
#
# The minimum required version of Cppyy can be specified using the
# standard syntax, e.g.  find_package(Cppyy 4.19)
#

find_program(Cppyy_EXECUTABLE NAMES rootcling)

if(Cppyy_EXECUTABLE)
  #
  # Cppyy_DIR.
  #
  set(Cppyy_DIR ${CMAKE_CURRENT_LIST_DIR})
  #
  # Cppyy_INCLUDE_DIRS.
  #
  get_filename_component(Cppyy_INCLUDE_DIRS ${Cppyy_DIR} DIRECTORY)
  set(Cppyy_INCLUDE_DIRS "${Cppyy_INCLUDE_DIRS}/include")
  #
  # Cppyy_VERSION.
  #
  find_package(ROOT QUIET REQUIRED PATHS ${CMAKE_CURRENT_LIST_DIR})
  if(ROOT_FOUND)
    set(Cppyy_VERSION ${ROOT_VERSION})
  endif()
endif()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
    Cppyy
    REQUIRED_VARS Cppyy_EXECUTABLE Cppyy_DIR Cppyy_INCLUDE_DIRS
    VERSION_VAR Cppyy_VERSION)

mark_as_advanced(Cppyy_VERSION)

#
# Generate a set of bindings from a set of C/C++ header files.
#
#   CPPYY_ADD_BINDINGS(
#       pkg_lib
#       pkg_version
#       [LANGUAGE_STANDARD std]
#       [LINKDEFS linkdef...]
#       [IMPORTS pcm...]
#       [GENERATE_OPTIONS option...]
#       [COMPILE_OPTIONS option...]
#       [INCLUDE_DIRS dir...]
#       [LINK_LIBRARIES library...]
#       [H_DIRS H_DIRSectory]
#       H_FILES h_file...)
#
# Arguments and options:
#
#   pkg_lib             The name of the library to generate.
#
#                       TODO: The name does not contribute to the Python
#                       namespace.
#
#   pkg_version         The version of the library to generate.
#
#   LANGUAGE_STANDARD std
#                       The version of C++ in use, "14" by default.
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
#   GENERATE_OPTIONS option
#                       Options which are to be passed into the rootcling
#                       command. For example, bindings which depend on Qt
#                       may need "-D__PIC__;-Wno-macro-redefined" as per
#                       https://sft.its.cern.ch/jira/browse/ROOT-8719.
#
#   COMPILE_OPTIONS option
#                       Options which are to be passed into the compile/link
#                       command.
#
#   INCLUDE_DIRS dir    Include directories.
#
#   LINK_LIBRARIES library
#                       Libraries to link against.
#
#   H_DIRS directory    Base directories for H_FILES.
#
#   H_FILES h_file      Header files for which to generate bindings in pkg_lib.
#                       Absolute filenames, or filenames relative to H_DIRS. All
#                       definitions found directly in these files will contribute
#                       to the bindings. (NOTE: This means that if "forwarding
#                       headers" are present, the real "legacy" headers must be
#                       specified as H_FILES).
#
# Examples:
#
#   find_package(Qt5Core NO_MODULE)
#   find_package(KF5KDcraw NO_MODULE)
#   get_target_property(_H_DIRS KF5::KDcraw INTERFACE_INCLUDE_DIRECTORIES)
#   get_target_property(_LINK_LIBRARIES KF5::KDcraw INTERFACE_LINK_LIBRARIES)
#   set(_LINK_LIBRARIES KF5::KDcraw ${_LINK_LIBRARIES})
#   include(${KF5KDcraw_DIR}/KF5KDcrawConfigVersion.cmake)
#
#   CPPYY_ADD_BINDINGS(
#       "KDCRAW" "${PACKAGE_VERSION}"
#       LANGUAGE_STANDARD "14"
#       LINKDEFS "../linkdef_overrides.h"
#       GENERATE_OPTIONS "-D__PIC__;-Wno-macro-redefined"
#       INCLUDE_DIRS ${Qt5Core_INCLUDE_DIRS}
#       LINK_LIBRARIES ${_LINK_LIBRARIES}
#       H_DIRS ${_H_DIRS}
#       H_FILES "dcrawinfocontainer.h;kdcraw.h;rawdecodingsettings.h;rawfiles.h")
#
function(CPPYY_ADD_BINDINGS pkg_lib pkg_version)
  cmake_parse_arguments(
    ARG
    ""
    "LANGUAGE_STANDARD"
    "LINKDEFS;IMPORTS;GENERATE_OPTIONS;COMPILE_OPTIONS;INCLUDE_DIRS;LINK_LIBRARIES;H_DIRS;H_FILES"
    ${ARGN})
  if(NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(SEND_ERROR "Unexpected arguments specified '${ARG_UNPARSED_ARGUMENTS}'")
  endif()
  set(pkg_file ${CMAKE_SHARED_LIBRARY_PREFIX}${pkg_lib}${CMAKE_SHARED_LIBRARY_SUFFIX})
  #
  # Language standard.
  #
  if("${ARG_LANGUAGE_STANDARD}" STREQUAL "")
    set(ARG_LANGUAGE_STANDARD "14")
  endif()
  #
  # Make H_FILES with absolute paths.
  #
  if("${ARG_H_FILES}" STREQUAL "")
    message(SEND_ERROR "No H_FILES specified")
  endif()
  if("${ARG_H_DIRS}" STREQUAL "")
    set(ARG_H_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  set(tmp)
  foreach(h_file IN LISTS ARG_H_FILES)
    if(NOT IS_ABSOLUTE ${h_file})
      foreach(h_dir IN LISTS ARG_H_DIRS)
        if(EXISTS ${h_dir}/${h_file})
          set(h_file ${h_dir}/${h_file})
          break()
        endif()
      endforeach(h_dir)
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
    file(APPEND ${out_linkdef} "#pragma link C++ defined_in ${h_file};\n")
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
  list(APPEND cling_args "-std=c++${ARG_LANGUAGE_STANDARD}")
  foreach(dir ${ARG_H_DIRS} ${ARG_INCLUDE_DIRS})
    list(APPEND cling_args "-I${dir}")
  endforeach(dir)
  list(APPEND cling_args "${ARG_GENERATE_OPTIONS}")
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
  set_property(TARGET ${pkg_lib} PROPERTY CXX_STANDARD ${ARG_LANGUAGE_STANDARD})
  target_include_directories(${pkg_lib} PRIVATE ${Cppyy_INCLUDE_DIRS} ${ARG_H_DIRS} ${ARG_INCLUDE_DIRS})
  target_compile_options(${pkg_lib} PRIVATE ${ARG_COMPILE_OPTIONS})
  target_link_libraries(${pkg_lib} ${ARG_LINK_LIBRARIES})
  #
  # Install. NOTE: The generated files contain as few binding-specific strings
  # as possible.
  #
  file(
    GENERATE OUTPUT "${pkg_lib}.py"
    CONTENT "import os as _os
import sys as _sys

from cppyy_backend import bindings_utils as _utils

_pkg_dir, _pkg_py = _os.path.split(__file__)
_pkg_lib = _os.path.splitext(_pkg_py)[0]
_utils.rootmapper(_pkg_dir, _pkg_lib, '${CMAKE_SHARED_LIBRARY_PREFIX}', '${CMAKE_SHARED_LIBRARY_SUFFIX}', _sys.modules[_pkg_lib])
")
  file(
    GENERATE OUTPUT "setup.py"
    CONTENT "import os

from cppyy_backend import bindings_utils

pkg_dir = os.path.dirname(__file__)
pkg_lib = '${pkg_lib}'
bindings_utils.setup(pkg_dir, pkg_lib, '${CMAKE_SHARED_LIBRARY_PREFIX}', '${CMAKE_SHARED_LIBRARY_SUFFIX}', '${pkg_version}')
")
    install(CODE "execute_process(COMMAND python ${CMAKE_BINARY_DIR}/setup.py install)")
endfunction(CPPYY_ADD_BINDINGS)

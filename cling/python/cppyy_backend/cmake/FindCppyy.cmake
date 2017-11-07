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
#   cppyy_add_bindings(
#       pkg
#       pkg_version
#       author
#       author_email
#       [URL url]
#       [LICENSE license]
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
#   pkg                 The name of the package to generate. This can be either
#                       of the form "simplename" (e.g. "Akonadi"), or of the
#                       form "namespace.simplename" (e.g. "KF5.Akonadi").
#
#   pkg_version         The version of the package.
#
#   author              The name of the library author.
#
#   author_email        The email address of the library author.
#
#   URL url             The home page for the library. Default is
#                       "https://pypi.python.org/pypi/<pkg>".
#
#   LICENSE license     The license, default is "LGPL 2.0".
#
#   LANGUAGE_STANDARD std
#                       The version of C++ in use, "14" by default.
#
#   LINKDEFS linkdefs   Files or lines which contain custom content
#                       for the linkdef.h file used by rootcling. See
#                       https://root.cern.ch/root/html/guides/users-guide/AddingaClass.html#the-linkdef.h-file.
#
#   IMPORTS pcm         Files which contain previously-generated bindings
#                       which pkg depends on.
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
#   H_FILES h_file      Header files for which to generate bindings in pkg.
#                       Absolute filenames, or filenames relative to H_DIRS. All
#                       definitions found directly in these files will contribute
#                       to the bindings. (NOTE: This means that if "forwarding
#                       headers" are present, the real "legacy" headers must be
#                       specified as H_FILES).
#
#                       All header files which contribute to a given C++ namespace
#                       should be grouped into a single pkg to ensure a 1-to-1
#                       mapping with the implementing Python class.
#
# Returns via PARENT_SCOPE variables:
#
#   target              The CMake target used to build.
#
#   setup_py            The setup.py script used to build or install pkg.
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
#   cppyy_add_bindings(
#       "KDCRAW" "${PACKAGE_VERSION}" "Shaheed" "srhaque@theiet.org"
#       LANGUAGE_STANDARD "14"
#       LINKDEFS "../linkdef_overrides.h"
#       GENERATE_OPTIONS "-D__PIC__;-Wno-macro-redefined"
#       INCLUDE_DIRS ${Qt5Core_INCLUDE_DIRS}
#       LINK_LIBRARIES ${_LINK_LIBRARIES}
#       H_DIRS ${_H_DIRS}
#       H_FILES "dcrawinfocontainer.h;kdcraw.h;rawdecodingsettings.h;rawfiles.h")
#
function(cppyy_add_bindings pkg pkg_version author author_email)
  set(simple_args URL LICENSE LANGUAGE_STANDARD)
  set(list_args LINKDEFS IMPORTS GENERATE_OPTIONS COMPILE_OPTIONS INCLUDE_DIRS LINK_LIBRARIES H_DIRS H_FILES)
  cmake_parse_arguments(
    ARG
    ""
    "${simple_args}"
    "${list_args}"
    ${ARGN})
  if(NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(SEND_ERROR "Unexpected arguments specified '${ARG_UNPARSED_ARGUMENTS}'")
  endif()
  string(REGEX MATCH "[^\.]+$" pkg_simplename ${pkg})
  string(REGEX REPLACE "\.?${pkg_simplename}" "" pkg_namespace ${pkg})
  set(pkg_dir ${CMAKE_CURRENT_BINARY_DIR})
  string(REPLACE "." "/" tmp ${pkg})
  set(pkg_dir "${pkg_dir}/${tmp}")
  set(pkg_file ${CMAKE_SHARED_LIBRARY_PREFIX}${pkg_simplename}${CMAKE_SHARED_LIBRARY_SUFFIX})
  set(cpp_file ${CMAKE_CURRENT_BINARY_DIR}/${pkg_simplename}.cpp)
  set(pcm_file ${pkg_dir}/${pkg_simplename}_rdict.pcm)
  set(rootmap_file ${pkg_dir}/${pkg_simplename}.rootmap)
  #
  # Package metadata.
  #
  if("${ARG_URL}" STREQUAL "")
    string(REPLACE "." "-" tmp ${pkg})
    set(ARG_URL "https://pypi.python.org/pypi/${tmp}")
  endif()
  if("${ARG_LICENSE}" STREQUAL "")
    set(ARG_LICENSE "LGPL2.1")
  endif()
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
      if(NOT IS_ABSOLUTE ${in_linkdef} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${in_linkdef})
        set(in_linkdef ${CMAKE_CURRENT_SOURCE_DIR}/${in_linkdef})
      endif()
      if(EXISTS ${in_linkdef})
        file(APPEND ${out_linkdef} "/* Copied from ${in_linkdef}: */\n")
        file(STRINGS ${in_linkdef} in_linkdef NEWLINE_CONSUME)
      endif()
      file(APPEND ${out_linkdef} "${in_linkdef}\n")
    endforeach(in_linkdef)
  endif()
  #
  # Record diagnostics.
  #
  file(APPEND ${out_linkdef} "//\n// Diagnostics.\n//\n")
  foreach(arg IN LISTS simple_args list_args)
    file(APPEND ${out_linkdef} "// ${arg}=${ARG_${arg}}\n")
  endforeach(arg)
  #
  # Set up arguments for rootcling.
  #
  set(cling_args)
  list(APPEND cling_args "-f" ${cpp_file})
  list(APPEND cling_args "-s" ${pkg_simplename})
  foreach(in_pcm IN LISTS ARG_IMPORTS)
    #
    # Create -m options for any imported .pcm files.
    #
    list(APPEND cling_args "-m" "${in_pcm}")
  endforeach(in_pcm)
  list(APPEND cling_args "-rmf" ${rootmap_file} "-rml" ${pkg_file})
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
  file(MAKE_DIRECTORY ${pkg_dir})
  add_custom_command(OUTPUT ${cpp_file} ${pcm_file} ${rootmap_file}
                     COMMAND ${Cppyy_EXECUTABLE} ${cling_args}
                     WORKING_DIRECTORY ${pkg_dir})
  #
  # Compile/link.
  #
  add_library(${pkg_simplename} SHARED ${cpp_file})
  set_property(TARGET ${pkg_simplename} PROPERTY CXX_STANDARD ${ARG_LANGUAGE_STANDARD})
  set_property(TARGET ${pkg_simplename} PROPERTY LIBRARY_OUTPUT_DIRECTORY ${pkg_dir})
  target_include_directories(${pkg_simplename} PRIVATE ${Cppyy_INCLUDE_DIRS} ${ARG_H_DIRS} ${ARG_INCLUDE_DIRS})
  target_compile_options(${pkg_simplename} PRIVATE ${ARG_COMPILE_OPTIONS})
  target_link_libraries(${pkg_simplename} ${ARG_LINK_LIBRARIES})
  #
  # Install. NOTE: The generated files contain as few binding-specific strings
  # as possible.
  #
  file(
    GENERATE OUTPUT "${pkg_dir}/__init__.py"
    CONTENT "from cppyy_backend import bindings_utils

bindings_utils.rootmapper(__file__, '${CMAKE_SHARED_LIBRARY_PREFIX}', '${CMAKE_SHARED_LIBRARY_SUFFIX}', '${pkg_namespace}')
del bindings_utils
")
  set(setup_py ${CMAKE_CURRENT_BINARY_DIR}/setup.py)
  file(WRITE ${setup_py} "import os

from cppyy_backend import bindings_utils

pkg_dir = os.path.dirname(__file__)
pkg = '${pkg}'
bindings_utils.setup(pkg_dir, pkg, '${CMAKE_SHARED_LIBRARY_PREFIX}', '${CMAKE_SHARED_LIBRARY_SUFFIX}',
                     '${pkg_version}', '${author}', '${author_email}', '${ARG_URL}', '${ARG_LICENSE}')
")
  set(setup_cfg ${CMAKE_CURRENT_BINARY_DIR}/setup.cfg)
  file(WRITE ${setup_cfg} "[bdist_wheel]
universal=1
")
  #
  # Return results.
  #
  set(target ${pkg_simplename} PARENT_SCOPE)
  set(setup_py ${setup_py} PARENT_SCOPE)
endfunction(cppyy_add_bindings)

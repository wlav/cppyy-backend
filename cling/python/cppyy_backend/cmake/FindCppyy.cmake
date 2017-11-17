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
# The module also defines the following functions:
#
#   cppyy_add_bindings - Generate a set of bindings from a set of header files.
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
# Generate a set of bindings from a set of header files.
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
# The bindings are based on https://cppyy.readthedocs.io/en/latest/, and can be
# used as per the documentation provided via the cppyy.cgl namespace. The
# environment variable LD_LIBRARY_PATH must contain the path of the <pkg>.rootmap
# file. Use "import cppyy; from cppyy.gbl import <some-C++-entity>".
#
# Alternatively, use "import <pkg>". This convenience wrapper supports
# "discovery" of the available C++ entities using, for example Python 3's command
# line completion support.
#
# The bindings are complete with a setup.py, supporting both Wheel and Egg-based
# packaging, and a test.py supporting pytest/nosetest sanity test of the bindings.
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
#                       In lines, literal semi-colons must be escaped: "\;".
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
#   EXTRA_CODES code    Files which contain extra code needed by the bindings.
#
#   EXTRA_HEADERS hdr   Files which contain extra headers needed by the bindings.
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
  set(list_args LINKDEFS IMPORTS GENERATE_OPTIONS EXTRA_CODES EXTRA_HEADERS COMPILE_OPTIONS INCLUDE_DIRS
    LINK_LIBRARIES H_DIRS H_FILES)
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
  set(lib_name "${pkg_namespace}${pkg_simplename}Cppyy")
  set(lib_file ${CMAKE_SHARED_LIBRARY_PREFIX}${lib_name}${CMAKE_SHARED_LIBRARY_SUFFIX})
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
    #
    # Doubled-up path separators "//" causes errors in rootcling.
    #
    string(REGEX REPLACE "/+" "/" h_file ${h_file})
    file(APPEND ${out_linkdef} "#pragma link C++ defined_in ${h_file};\n")
  endforeach(h_file)
  foreach(h_file IN LISTS ARG_EXTRA_HEADERS)
    #
    # Doubled-up path separators "//" causes errors in rootcling.
    #
    string(REGEX REPLACE "/+" "/" h_file ${h_file})
    file(APPEND ${out_linkdef} "#pragma extra_include \"${h_file}\";\n")
  endforeach(h_file)
  #
  # Append any manually-provided linkdef.h content.
  #
  set(LINKDEF_EXTRACTS)
  string(REPLACE "\\" "\\\\" ARG_LINKDEFS "${ARG_LINKDEFS}")
  foreach(in_linkdef IN LISTS ARG_LINKDEFS)
    if("${in_linkdef}" STREQUAL "")
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${in_linkdef}" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${in_linkdef}")
      set(in_linkdef "${CMAKE_CURRENT_SOURCE_DIR}/${in_linkdef}")
    endif()
    if(EXISTS "${in_linkdef}")
      file(APPEND ${out_linkdef} "/* Copied from ${in_linkdef}: */\n")
      file(STRINGS ${in_linkdef} in_linkdef NEWLINE_CONSUME)
    else()
      file(APPEND ${out_linkdef} "/* Inlined: */\n")
    endif()
    string(REPLACE "\n" ";" in_linkdef "${in_linkdef}")
    foreach(line ${in_linkdef})
      file(APPEND ${out_linkdef} "${line}\n")
    endforeach()
    list(GET in_linkdef 0 in_linkdef)
    list(APPEND LINKDEFS_EXTRACTS ${in_linkdef})
  endforeach(in_linkdef)
  #
  # Record diagnostics.
  #
  file(APPEND ${out_linkdef} "//\n// Diagnostics.\n//\n")
  foreach(arg IN LISTS simple_args list_args)
    if(arg STREQUAL "LINKDEFS")
      file(APPEND ${out_linkdef} "// ${arg}=\n")
      foreach(in_linkdef IN LISTS LINKDEFS_EXTRACTS)
        file(APPEND ${out_linkdef} "//    ${in_linkdef}...\n")
      endforeach(in_linkdef)
    else()
      file(APPEND ${out_linkdef} "// ${arg}=${ARG_${arg}}\n")
    endif()
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
  list(APPEND cling_args "-rmf" ${rootmap_file} "-rml" ${lib_file})
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
  add_library(${lib_name} SHARED ${cpp_file} ${ARG_EXTRA_CODES})
  set_property(TARGET ${lib_name} PROPERTY VERSION ${version})
  set_property(TARGET ${lib_name} PROPERTY CXX_STANDARD ${ARG_LANGUAGE_STANDARD})
  set_property(TARGET ${lib_name} PROPERTY LIBRARY_OUTPUT_DIRECTORY ${pkg_dir})
  target_include_directories(${lib_name} PRIVATE ${Cppyy_INCLUDE_DIRS} ${ARG_H_DIRS} ${ARG_INCLUDE_DIRS})
  target_compile_options(${lib_name} PRIVATE ${ARG_COMPILE_OPTIONS})
  target_link_libraries(${lib_name} ${ARG_LINK_LIBRARIES})
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
  # Generate a pytest/nosetest sanity test script.
  #
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test.py "# pytest/nosetest sanity test script.
import logging
import os
import pydoc
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(__file__)
pkg = '${pkg}'
PIPS = {}


class Test(object):
    @classmethod
    def setup_class(klass):
        #
        # Make an attempt to check the verbosity setting (ignore quiet!).
        #
        verbose = [a for a in sys.argv[1:] if a.startswith(('-v', '--verbos'))]
        if verbose:
            logging.basicConfig(level=logging.DEBUG, format='%(asctime)s %(name)s %(levelname)s: %(message)s')
        else:
            logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')
        #
        # What pip version do we have, and what Python versions do they support?
        # The command 'pip -V' returns a string of the form:
        #
        #   pip 9.0.1 from /usr/lib/python2.7/dist-packages (python 2.7)
        #
        possible_pips = ['pip', 'pip2', 'pip3']
        global PIPS
        for pip in possible_pips:
            try:
                version = subprocess.check_output([pip, '-V'], cwd=SCRIPT_DIR)
            except subprocess.CalledProcessError:
                pass
            else:
                #
                # All pip variants that map onto a given Python version are de-duped.
                #
                version = version.rsplit('(', 1)[-1]
                version = version.split()[-1]
                PIPS[version] = pip
        #
        # We want the pip names as the key.
        #
        PIPS = {v: k for k, v in PIPS.items()}
        assert len(PIPS), 'No viable pip versions found'

    @classmethod
    def teardown_class(klass):
        pass

    def setUp(self):
        '''This method is run once before _each_ test method is executed'''

    def teardown(self):
        '''This method is run once after _each_ test method is executed'''

    def test_install(self):
        for pip in PIPS:
            subprocess.check_call([pip, 'install', '--force-reinstall', '.'], cwd=SCRIPT_DIR)

    def test_import(self):
        __import__(pkg)

    def test_help(self):
        pydoc.render_doc(pkg)

    def test_uninstall(self):
        for pip in PIPS:
            subprocess.check_call([pip, 'uninstall', '--yes', pkg], cwd=SCRIPT_DIR)
")
  #
  # Return results.
  #
  set(target ${lib_name} PARENT_SCOPE)
  set(setup_py ${setup_py} PARENT_SCOPE)
endfunction(cppyy_add_bindings)

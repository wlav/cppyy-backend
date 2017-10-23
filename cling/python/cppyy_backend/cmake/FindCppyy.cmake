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
  execute_process(
    COMMAND ${CppyyConfig_EXECUTABLE} --cmake
    RESULT_VARIABLE _STATUS
    OUTPUT_VARIABLE _STDOUT
    ERROR_VARIABLE _STDERR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(Cppyy_DIR ${_STDOUT})
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
    set(Cppyy_VERSION ${ROOT_VERSION})
  endif()
endif()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
    Cppyy
    REQUIRED_VARS Cppyy_EXECUTABLE CppyyConfig_EXECUTABLE Cppyy_DIR Cppyy_INCLUDE_DIRS
    VERSION_VAR Cppyy_VERSION)

mark_as_advanced(Cppyy_VERSION)

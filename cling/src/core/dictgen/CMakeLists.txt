# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT core/dictgen package
############################################################################

# Provides capabilities to parse Linkdef / selection files and generate
# dictionaries.
#
# These files are linked into rootcling_stage1 and libCling. rootcling accesses
# them through rootcling_impl.

ROOT_ADD_COMPILE_OPTIONS(-Wno-class-memaccess)

ROOT_OBJECT_LIBRARY(Dictgen
  src/BaseSelectionRule.cxx
  src/ClassSelectionRule.cxx
  src/DictSelectionReader.cxx
  src/LinkdefReader.cxx
  src/rootcling_impl.cxx
  src/Scanner.cxx
  src/SelectionRules.cxx
  src/TModuleGenerator.cxx
  src/VariableSelectionRule.cxx
  src/XMLReader.cxx
)

add_dependencies(Dictgen CLING)

target_include_directories(Dictgen PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/res
  ${CMAKE_CURRENT_SOURCE_DIR}/../foundation/res
  ${CMAKE_CURRENT_SOURCE_DIR}/../clingutils/res
  ${CLING_INCLUDE_DIRS}
)

# Register the llvm include directories after clangs. This instructs the compiler to resolve
# headers from our builtin clang. That's an issue when we are building with bultin_llvm=Off
# and we have installed clang headers, too.
target_include_directories(Dictgen SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CLING_CXXFLAGS}")
CHECK_CXX_COMPILER_FLAG("-fno-rtti" CXX_HAS_fno_rtti)
if(CXX_HAS_fno_rtti)
    set_source_files_properties(src/rootcling_impl.cxx   PROPERTIES COMPILE_FLAGS "-fno-rtti")
    set_source_files_properties(src/LinkdefReader.cxx    PROPERTIES COMPILE_FLAGS "-fno-rtti")
    set_source_files_properties(src/TModuleGenerator.cxx PROPERTIES COMPILE_FLAGS "-fno-rtti")
endif()

#---CreateRootClingCommandLineOptions------------------------------------------------------------------
generateHeader(Dictgen
  ${CMAKE_SOURCE_DIR}/core/dictgen/src/rootcling-argparse.py
  ${CMAKE_BINARY_DIR}/include/rootclingCommandLineOptionsHelp.h
)

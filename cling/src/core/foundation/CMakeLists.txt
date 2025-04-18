# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT core/foundation package
############################################################################

set(Foundation_dict_headers
  ESTLType.h
  TClassEdit.h
  ROOT/RMakeUnique.hxx
  ROOT/RSpan.hxx
  ROOT/TypeTraits.hxx
  PARENT_SCOPE
)

set(sources
  src/FoundationUtils.cxx
  src/TClassEdit.cxx
)

ROOT_OBJECT_LIBRARY(Foundation ${sources})
target_include_directories(Foundation PRIVATE res)

ROOT_OBJECT_LIBRARY(Foundation_Stage1 ${sources}) # used by rootcling_stage1
target_include_directories(Foundation_Stage1 PRIVATE res)
set_target_properties(Foundation_Stage1 PROPERTIES COMPILE_FLAGS "${COMPILE_FLAGS} ${CLING_CXXFLAGS}")

ROOT_INSTALL_HEADERS()

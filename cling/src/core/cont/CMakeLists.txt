# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT core/cont package
############################################################################

set(Cont_dict_headers
  TArrayC.h
  TArray.h
  TBits.h
  TClassTable.h
  TCollection.h
  TCollectionProxyInfo.h
  TExMap.h
  THashList.h
  THashTable.h
  TIterator.h
  TList.h
  TMap.h
  TObjArray.h
  TObjectTable.h
  TOrdCollection.h
  TSeqCollection.h
  TVirtualCollectionProxy.h
  PARENT_SCOPE
)

ROOT_OBJECT_LIBRARY(Cont
  src/TArrayC.cxx
  src/TArray.cxx
  src/TBits.cxx
  src/TClassTable.cxx
  src/TCollection.cxx
  src/TExMap.cxx
  src/THashList.cxx
  src/THashTable.cxx
  src/TIterator.cxx
  src/TList.cxx
  src/TMap.cxx
  src/TObjArray.cxx
  src/TObjectTable.cxx
  src/TOrdCollection.cxx
  src/TSeqCollection.cxx
)

target_include_directories(Cont PRIVATE res ../foundation/res)

ROOT_INSTALL_HEADERS()

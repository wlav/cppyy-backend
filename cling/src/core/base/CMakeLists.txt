# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT core/base package
############################################################################

include_directories(res ../foundation/res)

set(BASE_HEADERS
  Bytes.h
  Byteswap.h
  Riostream.h
  Rtypes.h
  TApplication.h
  TBuffer.h
  TDatime.h
  TDirectory.h
  TEnv.h
  TError.h
  TException.h
  TInetAddress.h
  TMathBase.h
  TMD5.h
  TMemberInspector.h
  TNamed.h
  TObject.h
  TObjString.h
  TProcessID.h
  TProcessUUID.h
  TRegexp.h
  TROOT.h
  TStorage.h
  TString.h
  TSysEvtHandler.h
  TSystem.h
  TThreadSlots.h
  TTime.h
  TTimeStamp.h
  TUrl.h
  TUUID.h
  TVersionCheck.h
  TVirtualMutex.h
  TVirtualRWMutex.h
)

set(BASE_SOURCES
  src/Match.cxx
  src/String.cxx
  src/Stringio.cxx
  src/TApplication.cxx
  src/TBuffer.cxx
  src/TDatime.cxx
  src/TDirectory.cxx
  src/TEnv.cxx
  src/TError.cxx
  src/TException.cxx
  src/TInetAddress.cxx
  src/TListOfTypes.cxx
  src/TListOfTypes.h
  src/TMathBase.cxx
  src/TMD5.cxx
  src/TMemberInspector.cxx
  src/TNamed.cxx
  src/TObject.cxx
  src/TObjString.cxx
  src/TProcessID.cxx
  src/TProcessUUID.cxx
  src/TRegexp.cxx
  src/TStorage.cxx
  src/TString.cxx
  src/TSysEvtHandler.cxx
  src/TSystem.cxx
  src/TTime.cxx
  src/TTimeStamp.cxx
  src/TUrl.cxx
  src/TUUID.cxx
  src/TVirtualMutex.cxx
)

# only here complete list of headers can be propogated to parent cmake file
set(CoreLegacy_dict_headers ${BASE_HEADERS} PARENT_SCOPE)

ROOT_OBJECT_LIBRARY(Base ${BASE_SOURCES})
target_include_directories(Base PRIVATE ${PCRE_INCLUDE_DIR})

ROOT_INSTALL_HEADERS(${BASE_HEADER_DIRS})

# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

############################################################################
# CMakeLists.txt file for building ROOT core/meta package
############################################################################

set(Meta_dict_headers
  TBaseClass.h
  TClassGenerator.h
  TClass.h
  TClassRef.h
  TClassStreamer.h
  TDataMember.h
  TDataType.h
  TDictAttributeMap.h
  TDictionary.h
  TEnumConstant.h
  TEnum.h
  TFunction.h
  TFunctionTemplate.h
  TGenericClassInfo.h
  TGlobal.h
  TInterpreter.h
  TInterpreterValue.h
  TIsAProxy.h
  TListOfDataMembers.h
  TListOfEnums.h
  TListOfEnumsWithLock.h
  TListOfFunctions.h
  TListOfFunctionTemplates.h
  TMemberStreamer.h
  TMethodArg.h
  TMethod.h
  TProtoClass.h
  TRealData.h
  TStreamerElement.h
  TStreamer.h
  TVirtualIsAProxy.h
  TVirtualStreamerInfo.h
  PARENT_SCOPE
)

ROOT_OBJECT_LIBRARY(Meta
  src/TBaseClass.cxx
  src/TClass.cxx
  src/TClassGenerator.cxx
  src/TClassRef.cxx
  src/TDataMember.cxx
  src/TDataType.cxx
  src/TDictAttributeMap.cxx
  src/TDictionary.cxx
  src/TEnum.cxx
  src/TEnumConstant.cxx
  src/TFunction.cxx
  src/TFunctionTemplate.cxx
  src/TGenericClassInfo.cxx
  src/TGlobal.cxx
  src/TInterpreter.cxx
  src/TIsAProxy.cxx
  src/TListOfDataMembers.cxx
  src/TListOfEnums.cxx
  src/TListOfEnumsWithLock.cxx
  src/TListOfFunctions.cxx
  src/TListOfFunctionTemplates.cxx
  src/TMethod.cxx
  src/TMethodArg.cxx
  src/TProtoClass.cxx
  src/TRealData.cxx
  src/TStreamerElement.cxx
  src/TVirtualStreamerInfo.cxx
)

target_include_directories(Meta PRIVATE res ../foundation/res)

ROOT_INSTALL_HEADERS()

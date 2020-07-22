/* @(#)root/base:$Id$ */

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifdef __CINT__

#pragma extra_include "string";
#pragma create TClass std::string;

#pragma link C++ namespace CppyyLegacy;

#pragma link C++ enum CppyyLegacy::EAccessMode;
#pragma link C++ enum CppyyLegacy::ESignals;
#pragma link C++ enum CppyyLegacy::ESysConstants;

#pragma link C++ struct CppyyLegacy::FileStat_t;
#pragma link C++ struct CppyyLegacy::UserGroup_t;
#pragma link C++ struct CppyyLegacy::RedirectHandle_t;

#pragma link C++ class CppyyLegacy::TMD5+;
#pragma link C++ class CppyyLegacy::TMemberInspector;
#pragma link C++ class CppyyLegacy::TNamed+;
#pragma link C++ class CppyyLegacy::TObjString+;
#pragma link C++ class CppyyLegacy::TObject-;
#pragma link C++ class CppyyLegacy::TProcessID+;
#pragma link C++ class CppyyLegacy::TProcessUUID+;
#pragma link C++ class CppyyLegacy::TROOT;
#pragma link C++ class CppyyLegacy::TRegexp;
#pragma link C++ class CppyyLegacy::TSignalHandler;
#pragma link C++ class CppyyLegacy::TStdExceptionHandler;
#pragma link C++ class CppyyLegacy::TStorage;
#pragma link C++ class CppyyLegacy::TString-!;
#pragma link off class CppyyLegacy::TString::Rep_t;
#pragma link C++ class CppyyLegacy::TSubString;
#pragma link C++ class CppyyLegacy::TSystem+;
#pragma link C++ class CppyyLegacy::TTime;
#pragma link C++ class CppyyLegacy::TUUID+;

#endif

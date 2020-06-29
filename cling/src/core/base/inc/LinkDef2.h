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

#pragma link C++ enum EAccessMode;
#pragma link C++ enum ESignals;
#pragma link C++ enum ESysConstants;

#pragma link C++ struct FileStat_t;
#pragma link C++ struct UserGroup_t;
#pragma link C++ struct RedirectHandle_t;

#pragma link C++ class TFolder+;
#pragma link C++ class TMD5+;
#pragma link C++ class TMemberInspector;
#pragma link C++ class TNamed+;
#pragma link C++ class TObjString+;
#pragma link C++ class TObject-;
#pragma link C++ class TProcessID+;
#pragma link C++ class TProcessUUID+;
#pragma link C++ class TROOT;
#pragma link C++ class TRegexp;
#pragma link C++ class TSignalHandler;
#pragma link C++ class TStdExceptionHandler;
#pragma link C++ class TStopwatch+;
#pragma link C++ class TStorage;
#pragma link C++ class TString-!;
#pragma link off class TString::Rep_t;
#pragma link C++ class TSubString;
#pragma link C++ class TSystem+;
#pragma link C++ class TTime;
#pragma link C++ class TUUID+;

#endif

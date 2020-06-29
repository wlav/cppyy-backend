/* @(#)root/base:$Id$ */

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifdef __CINT__

#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ global kTRUE;
#pragma link C++ global kFALSE;
#pragma link C++ global kMaxUChar;
#pragma link C++ global kMaxChar;
#pragma link C++ global kMinChar;
#pragma link C++ global kMaxUShort;
#pragma link C++ global kMaxShort;
#pragma link C++ global kMinShort;
#pragma link C++ global kMaxUInt;
#pragma link C++ global kMaxInt;
#pragma link C++ global kMinInt;
#pragma link C++ global kMaxULong;
#pragma link C++ global kMaxLong;
#pragma link C++ global kMinLong;
#pragma link C++ global kMaxULong64;
#pragma link C++ global kMaxLong64;
#pragma link C++ global kMinLong64;
#pragma link C++ global kBitsPerByte;
#pragma link C++ global kNPOS;
#pragma link C++ global kInfo;
#pragma link C++ global kWarning;
#pragma link C++ global kError;
#pragma link C++ global kSysError;
#pragma link C++ global kFatal;

#pragma link C++ enum EObjBits;
#pragma link C++ enum EEnvLevel;
#pragma link C++ enum EColor;
#pragma link C++ enum ELineStyle;
#pragma link C++ enum EMarkerStyle;

// This is not a global anymore but a function
// gROOT is a preprocessor macro #define gROOT (ROOT::GetROOT()
// #pragma link C++ global gROOT;
#pragma link C++ global gEnv;
#pragma link C++ global gSystem;
#pragma link C++ global gApplication;
#pragma link C++ global gDebug;
#pragma link C++ global gErrorIgnoreLevel;
#pragma link C++ global gErrorAbortLevel;
#pragma link C++ global gPrintViaErrorHandler;
#pragma link C++ global gRootDir;
#pragma link C++ global gProgName;
#pragma link C++ global gProgPath;

#pragma link C++ function Info;
#pragma link C++ function Warning;
#pragma link C++ function Error;
#pragma link C++ function SysError;
#pragma link C++ function Fatal;
#pragma link C++ function Obsolete;
#pragma link C++ function Form;
#pragma link C++ function Printf;
#pragma link C++ function Strip;
#pragma link C++ function StrDup;
#pragma link C++ function Compress;
#pragma link C++ function EscChar;
#pragma link C++ function UnEscChar;
#pragma link C++ function Hash(const char*);

// New representation of the pragma below
// #pragma link C++ global gROOT;
// a preprocessor statement transformed gROOT in a function call, ROOT::GetROOT().
#pragma link C++ function ROOT::GetROOT();

#pragma link C++ nestedtypedef;
#pragma link C++ namespace ROOT;
#pragma create TClass TMath;
#pragma link C++ function TMath::BinarySearch(Long64_t, const Short_t*, Short_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Int_t*, Int_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Float_t*, Float_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Double_t*, Double_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Long_t*, Long_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Long64_t*, Long64_t);

#pragma link C++ function TMath::BinarySearch(Long64_t, const Short_t**, Short_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Int_t**, Int_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Float_t**, Float_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Double_t**, Double_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Long_t**, Long_t);
#pragma link C++ function TMath::BinarySearch(Long64_t, const Long64_t**, Long64_t);

#pragma link C++ function TMath::Sort(Long64_t, const Short_t*, Long64_t*, Bool_t);
#pragma link C++ function TMath::Sort(Long64_t, const Int_t*, Long64_t*, Bool_t);
#pragma link C++ function TMath::Sort(Long64_t, const Float_t*, Long64_t*, Bool_t);
#pragma link C++ function TMath::Sort(Long64_t, const Double_t*, Long64_t*, Bool_t);
#pragma link C++ function TMath::Sort(Long64_t, const Long_t*, Long64_t*, Bool_t);
#pragma link C++ function TMath::Sort(Long64_t, const Long64_t*, Long64_t*, Bool_t);

#pragma link C++ function TMath::Sort(Int_t, const Short_t*, Int_t*, Bool_t);
#pragma link C++ function TMath::Sort(Int_t, const Int_t*, Int_t*, Bool_t);
#pragma link C++ function TMath::Sort(Int_t, const Float_t*, Int_t*, Bool_t);
#pragma link C++ function TMath::Sort(Int_t, const Double_t*, Int_t*, Bool_t);
#pragma link C++ function TMath::Sort(Int_t, const Long_t*, Int_t*, Bool_t);
#pragma link C++ function TMath::Sort(Int_t, const Long64_t*, Int_t*, Bool_t);

#pragma link C++ class TApplication;
#pragma link C++ class TApplicationImp;
#pragma link C++ class TBuffer;
#pragma link C++ class TDatime-;
#pragma link C++ class TDirectory-;
#pragma link C++ class TEnv+;
#pragma link C++ class TEnvRec+;
// Those are NOT going to be saved ... so no need for a +

#endif

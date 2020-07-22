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

#pragma link C++ namespace CppyyLegacy;

#pragma link C++ global CppyyLegacy::kTRUE;
#pragma link C++ global CppyyLegacy::kFALSE;
#pragma link C++ global CppyyLegacy::kMaxUChar;
#pragma link C++ global CppyyLegacy::kMaxChar;
#pragma link C++ global CppyyLegacy::kMinChar;
#pragma link C++ global CppyyLegacy::kMaxUShort;
#pragma link C++ global CppyyLegacy::kMaxShort;
#pragma link C++ global CppyyLegacy::kMinShort;
#pragma link C++ global CppyyLegacy::kMaxUInt;
#pragma link C++ global CppyyLegacy::kMaxInt;
#pragma link C++ global CppyyLegacy::kMinInt;
#pragma link C++ global CppyyLegacy::kMaxULong;
#pragma link C++ global CppyyLegacy::kMaxLong;
#pragma link C++ global CppyyLegacy::kMinLong;
#pragma link C++ global CppyyLegacy::kMaxULong64;
#pragma link C++ global CppyyLegacy::kMaxLong64;
#pragma link C++ global CppyyLegacy::kMinLong64;
#pragma link C++ global CppyyLegacy::kBitsPerByte;
#pragma link C++ global CppyyLegacy::kNPOS;
#pragma link C++ global CppyyLegacy::kInfo;
#pragma link C++ global CppyyLegacy::kWarning;
#pragma link C++ global CppyyLegacy::kError;
#pragma link C++ global CppyyLegacy::kSysError;
#pragma link C++ global CppyyLegacy::kFatal;

#pragma link C++ enum CppyyLegacy::EObjBits;
#pragma link C++ enum CppyyLegacy::EEnvLevel;

#pragma link C++ global CppyyLegacy::gEnv;
#pragma link C++ global gSystem;
#pragma link C++ global CppyyLegacy::gApplication;
#pragma link C++ global CppyyLegacy::gDebug;
#pragma link C++ global CppyyLegacy::gErrorIgnoreLevel;
#pragma link C++ global CppyyLegacy::gErrorAbortLevel;
#pragma link C++ global CppyyLegacy::gPrintViaErrorHandler;
#pragma link C++ global CppyyLegacy::gRootDir;
#pragma link C++ global CppyyLegacy::gProgName;
#pragma link C++ global CppyyLegacy::gProgPath;

#pragma link C++ function CppyyLegacy::Info;
#pragma link C++ function CppyyLegacy::Warning;
#pragma link C++ function CppyyLegacy::Error;
#pragma link C++ function CppyyLegacy::SysError;
#pragma link C++ function CppyyLegacy::Fatal;
#pragma link C++ function CppyyLegacy::Form;
#pragma link C++ function CppyyLegacy::Printf;
#pragma link C++ function CppyyLegacy::Strip;
#pragma link C++ function CppyyLegacy::StrDup;
#pragma link C++ function CppyyLegacy::Compress;
#pragma link C++ function CppyyLegacy::EscChar;
#pragma link C++ function CppyyLegacy::UnEscChar;
#pragma link C++ function CppyyLegacy::Hash(const char*);

// New representation of the pragma below
// #pragma link C++ global gROOT;
// a preprocessor statement transformed gROOT in a function call, CppyyLegacy::GetROOT().
#pragma link C++ function CppyyLegacy::GetROOT();

#pragma link C++ nestedtypedef;
#pragma create TClass CppyyLegacy::TMath;

#pragma link C++ class CppyyLegacy::TApplication;
#pragma link C++ class CppyyLegacy::TBuffer;
#pragma link C++ class CppyyLegacy::TDatime-;
#pragma link C++ class CppyyLegacy::TDirectory-;
#pragma link C++ class CppyyLegacy::TEnv+;
#pragma link C++ class CppyyLegacy::TEnvRec+;

#endif

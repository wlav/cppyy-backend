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

#pragma link C++ typedef timespec_t;
#pragma link C++ struct timespec;

#pragma link C++ class CppyyLegacy::TTimeStamp+;
#pragma link C++ class CppyyLegacy::TVirtualMutex;
#pragma link C++ class CppyyLegacy::TVirtualRWMutex;
#pragma link C++ class CppyyLegacy::TLockGuard;
#pragma link C++ class CppyyLegacy::TReadLockGuard;
#pragma link C++ class CppyyLegacy::TWriteLockGuard;
#pragma link C++ class CppyyLegacy::TUrl;
#pragma link C++ class CppyyLegacy::TInetAddress-;

// Insure the creation of the TClass object for pairs that might be
// inside the cintdlls.
#pragma extra_include "string";
// insure using namespace std and declaration of std::pair
#pragma extra_include "Rpair.h";
#include <utility>

#pragma link C++ class std::pair<char*,int>+;
#pragma link C++ class std::pair<char*,long>+;
#pragma link C++ class std::pair<char*,float>+;
#pragma link C++ class std::pair<char*,double>+;
#pragma link C++ class std::pair<char*,void*>+;
#pragma link C++ class std::pair<char*,char*>+;
#pragma link C++ class std::pair<std::string,int>+;
#pragma link C++ class std::pair<std::string,long>+;
#pragma link C++ class std::pair<std::string,float>+;
#pragma link C++ class std::pair<std::string,double>+;
#pragma link C++ class std::pair<std::string,void*>+;
#pragma link C++ class std::pair<int,int>+;
#pragma link C++ class std::pair<int,long>+;
#pragma link C++ class std::pair<int,float>+;
#pragma link C++ class std::pair<int,double>+;
#pragma link C++ class std::pair<int,void*>+;
#pragma link C++ class std::pair<int,char*>+;
#pragma link C++ class std::pair<long,int>+;
#pragma link C++ class std::pair<long,long>+;
#pragma link C++ class std::pair<long,float>+;
#pragma link C++ class std::pair<long,double>+;
#pragma link C++ class std::pair<long,void*>+;
#pragma link C++ class std::pair<long,char*>+;
#pragma link C++ class std::pair<float,int>+;
#pragma link C++ class std::pair<float,long>+;
#pragma link C++ class std::pair<float,float>+;
#pragma link C++ class std::pair<float,double>+;
#pragma link C++ class std::pair<float,void*>+;
#pragma link C++ class std::pair<float,char*>+;
#pragma link C++ class std::pair<double,int>+;
#pragma link C++ class std::pair<double,long>+;
#pragma link C++ class std::pair<double,float>+;
#pragma link C++ class std::pair<double,double>+;
#pragma link C++ class std::pair<double,void*>+;
#pragma link C++ class std::pair<double,char*>+;

#pragma link C++ class std::pair<const char*,int>+;
#pragma link C++ class std::pair<const char*,long>+;
#pragma link C++ class std::pair<const char*,float>+;
#pragma link C++ class std::pair<const char*,double>+;
#pragma link C++ class std::pair<const char*,void*>+;
#pragma link C++ class std::pair<const char*,char*>+;
#pragma link C++ class std::pair<const std::string,int>+;
#pragma link C++ class std::pair<const std::string,long>+;
#pragma link C++ class std::pair<const std::string,float>+;
#pragma link C++ class std::pair<const std::string,double>+;
#pragma link C++ class std::pair<const std::string,void*>+;
#pragma link C++ class std::pair<const int,int>+;
#pragma link C++ class std::pair<const int,long>+;
#pragma link C++ class std::pair<const int,float>+;
#pragma link C++ class std::pair<const int,double>+;
#pragma link C++ class std::pair<const int,void*>+;
#pragma link C++ class std::pair<const int,char*>+;
#pragma link C++ class std::pair<const long,int>+;
#pragma link C++ class std::pair<const long,long>+;
#pragma link C++ class std::pair<const long,float>+;
#pragma link C++ class std::pair<const long,double>+;
#pragma link C++ class std::pair<const long,void*>+;
#pragma link C++ class std::pair<const long,char*>+;
#pragma link C++ class std::pair<const float,int>+;
#pragma link C++ class std::pair<const float,long>+;
#pragma link C++ class std::pair<const float,float>+;
#pragma link C++ class std::pair<const float,double>+;
#pragma link C++ class std::pair<const float,void*>+;
#pragma link C++ class std::pair<const float,char*>+;
#pragma link C++ class std::pair<const double,int>+;
#pragma link C++ class std::pair<const double,long>+;
#pragma link C++ class std::pair<const double,float>+;
#pragma link C++ class std::pair<const double,double>+;
#pragma link C++ class std::pair<const double,void*>+;
#pragma link C++ class std::pair<const double,char*>+;

#pragma extra_include "Rtypes.h";

#endif

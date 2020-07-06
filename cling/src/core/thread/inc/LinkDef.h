/* @(#)root/thread:$Id$ */

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

#pragma link C++ class CppyyLegacy::TThread;
#pragma link C++ class CppyyLegacy::TConditionImp;
#pragma link C++ class CppyyLegacy::TCondition;
#pragma link C++ class CppyyLegacy::TMutex;
#pragma link C++ class CppyyLegacy::TSpinMutex;
#pragma link C++ class CppyyLegacy::TMutexImp;
#ifndef _WIN32
#pragma link C++ class CppyyLegacy::TPosixCondition;
#pragma link C++ class CppyyLegacy::TPosixMutex;
#pragma link C++ class CppyyLegacy::TPosixThread;
#pragma link C++ class CppyyLegacy::TPosixThreadFactory;
#else
#pragma link C++ class CppyyLegacy::TWin32Condition;
#pragma link C++ class CppyyLegacy::TWin32Mutex;
#pragma link C++ class CppyyLegacy::TWin32Thread;
#pragma link C++ class CppyyLegacy::TWin32ThreadFactory;
#endif
#pragma link C++ class CppyyLegacy::TThreadFactory;
#pragma link C++ class CppyyLegacy::TThreadImp;

#endif

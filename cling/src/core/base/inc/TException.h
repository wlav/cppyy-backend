// @(#)root/base:$Id$
// Author: Fons Rademakers   21/09/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TException
#define ROOT_TException

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Exception Handling                                                   //
//                                                                      //
// Provide some macro's to simulate the coming C++ try, catch and throw //
// exception handling functionality.                                    //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#ifndef __CINT__
#include <setjmp.h>
#else
struct sigjmp_buf;
struct jmp_buf;
#endif

#include <ROOT/RConfig.hxx>
#include "DllImport.h"


namespace CppyyLegacy {

struct ExceptionContext_t {
#ifdef NEED_SIGJMP
   sigjmp_buf fBuf;
#else
   jmp_buf fBuf;
#endif
};

#ifdef NEED_SIGJMP
#define CLING_EXCEPTION_SETJMP(buf) sigsetjmp(buf,1)
#else
#define CLING_EXCEPTION_SETJMP(buf) setjmp(buf)
#endif

#define CLING_EXCEPTION_RETRY \
   { \
      static ExceptionContext_t R__curr, *R__old = gException; \
      int R__code; \
      gException = &R__curr; \
      R__code = CLING_EXCEPTION_SETJMP(gException->fBuf); if (R__code) { }; {

#define CLING_EXCEPTION_TRY \
   { \
      static ExceptionContext_t R__curr, *R__old = gException; \
      int R__code; \
      gException = &R__curr; \
      if ((R__code = CLING_EXCEPTION_SETJMP(gException->fBuf)) == 0) {

#define CLING_EXCEPTION_CATCH(n) \
         gException = R__old; \
      } else { \
         int n = R__code; \
         gException = R__old;

#define CLING_EXCEPTION_ENDTRY \
      } \
      gException = R__old; \
   }

R__EXTERN void Throw(int code);

class TExceptionHandler {
public:
   virtual ~TExceptionHandler() {}
   virtual void HandleException(int sig) = 0;
};

R__EXTERN TExceptionHandler* gExceptionHandler;

} // namespace CppyyLegacy

R__EXTERN CppyyLegacy::ExceptionContext_t *gException;

#endif

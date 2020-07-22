// @(#)root/base:$Id$
// Author: Fons Rademakers   22/12/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TApplication
#define ROOT_TApplication

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TApplication                                                         //
//                                                                      //
// This class creates the ROOT Application Environment that interfaces  //
// to the windowing system eventloop and eventhandlers.                 //
// This class must be instantiated exactly once in any given            //
// application. Normally the specific application class inherits from   //
// TApplication (see TRint).                                            //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TObject.h"


namespace CppyyLegacy {

class TSignalHandler;

class TApplication : public TObject {

public:
   // TApplication specific bits
   enum EStatusBits {
      kDefaultApplication = BIT(16)  // TRUE if created via CreateApplication()
   };
   enum EExitOnException {
      kDontExit,
      kExit,
      kAbort
   };

private:
   Int_t              fArgc;            //Number of com   mand line arguments
   char             **fArgv;            //Command line arguments
   EExitOnException   fExitOnException; //Exit on exception option

   TApplication(const TApplication&);             // not implemented
   TApplication& operator=(const TApplication&);  // not implemented

protected:
   static TList      *fgApplications;  //List of available applications

   TApplication();

public:
   TApplication(const char *appClassName, Int_t *argc, char **argv,
                void *options = 0, Int_t numOptions = 0);
   virtual ~TApplication();

   virtual void    HandleException(Int_t sig);
   virtual Bool_t  HandleTermInput() { return kFALSE; }
   EExitOnException ExitOnException(EExitOnException opt = kExit);

   static void     CreateApplication();

   ClassDef(TApplication,0)  //GUI application singleton
};

R__EXTERN TApplication *gApplication;

} // namespace CppyyLegacy

#endif

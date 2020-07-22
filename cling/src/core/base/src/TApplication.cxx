// @(#)root/base:$Id$
// Author: Fons Rademakers   22/12/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TApplication
\ingroup Base

This class creates the ROOT Application Environment that interfaces
to the windowing system eventloop and eventhandlers.
This class must be instantiated exactly once in any given
application. Normally the specific application class inherits from
TApplication (see TRint).
*/

#include "RConfigure.h"
#include "Riostream.h"
#include "TApplication.h"
#include "TException.h"
#include "TROOT.h"
#include "TSystem.h"
#include "TString.h"
#include "TError.h"
#include "TObjArray.h"
#include "TObjString.h"
#include "TInterpreter.h"
#include "TEnv.h"
#include "TClassTable.h"
#include "TClassTable.h"
#include "TUrl.h"
#include "TVirtualMutex.h"
#include "TClassEdit.h"
#include "TMethod.h"
#include "TDataMember.h"
#include <stdlib.h>


ClassImp(CppyyLegacy::TApplication);

namespace CppyyLegacy {

#define Printf TStringPrintf
TApplication *gApplication = 0;
TList *TApplication::fgApplications = 0;  // List of available applications

////////////////////////////////////////////////////////////////////////////////

static void CallEndOfProcessCleanups()
{
   // Insure that the files, canvases and sockets are closed.

   // If we get here, the tear down has started.  We have no way to know what
   // has or has not yet been done.  In particular on Ubuntu, this was called
   // after the function static in TSystem.cxx has been destructed.  So we
   // set gROOT in its end-of-life mode which prevents executing code, like
   // autoloading libraries (!) that is pointless ...
   if (gROOT) {
      gROOT->SetBit(kInvalidObject);
      gROOT->EndOfProcessCleanups();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Default ctor. Can be used by classes deriving from TApplication.

TApplication::TApplication() :
   fArgc(0), fArgv(0), fExitOnException(kDontExit)
{
}

////////////////////////////////////////////////////////////////////////////////
/// Create an application environment.

TApplication::TApplication(const char *appClassName, Int_t *argc, char **argv,
                           void * /*options*/, Int_t /* numOptions */) :
   fArgc(0), fArgv(0), fExitOnException(kDontExit)
{
   R__LOCKGUARD(gInterpreterMutex);

   // Create the list of applications the first time
   if (!fgApplications)
      fgApplications = new TList;

   // Add the new TApplication early, so that the destructor of the
   // default TApplication (if it is called in the block of code below)
   // created.
   fgApplications->Add(this);

   if (gApplication && gApplication->TestBit(kDefaultApplication)) {
      // allow default TApplication to be replaced by a "real" TApplication
      delete gApplication;
      gApplication = 0;
   }

   if (gApplication) {
      Error("TApplication", "only one instance of TApplication allowed");
      fgApplications->Remove(this);
      return;
   }

   if (!gROOT)
      ::CppyyLegacy::Fatal("TApplication::TApplication", "ROOT system not initialized");

   if (!gSystem)
      ::CppyyLegacy::Fatal("TApplication::TApplication", "gSystem not initialized");

   static Bool_t hasRegisterAtExit(kFALSE);
   if (!hasRegisterAtExit) {
      // If we are the first TApplication register the atexit)
      atexit(CallEndOfProcessCleanups);
      hasRegisterAtExit = kTRUE;
   }
   gROOT->SetName(appClassName);

   // copy command line arguments, can be later accessed via Argc() and Argv()
   if (argc && *argc > 0) {
      fArgc = *argc;
      fArgv = (char **)new char*[fArgc];
   }

   for (int i = 0; i < fArgc; i++)
      fArgv[i] = StrDup(argv[i]);

   if (fArgv)
      gSystem->SetProgname(fArgv[0]);

   // Tell TSystem the TApplication has been created
   gSystem->NotifyApplicationCreated();

   // Save current interpreter context
   gInterpreter->SaveContext();
   gInterpreter->SaveGlobalsContext();

   gROOT->SetLineHasBeenProcessed();

   //Needs to be done last
   gApplication = this;
   gROOT->SetApplication(this);

}

////////////////////////////////////////////////////////////////////////////////
/// TApplication dtor.

TApplication::~TApplication()
{
   for (int i = 0; i < fArgc; i++)
      if (fArgv[i]) delete [] fArgv[i];
   delete [] fArgv;

   if (fgApplications)
      fgApplications->Remove(this);

   // Reduce the risk of the files or sockets being closed after the
   // end of 'main' (or more exactly before the library start being
   // unloaded).
   if (fgApplications == 0 || fgApplications->FirstLink() == 0 ) {
      TROOT::ShutDown();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Handle exceptions (kSigBus, kSigSegmentationViolation,
/// kSigIllegalInstruction and kSigFloatingException) trapped in TSystem.
/// Specific TApplication implementations may want something different here.

void TApplication::HandleException(Int_t sig)
{
   if (TROOT::Initialized()) {
      if (gException) {
         gInterpreter->RewindDictionary();
         gInterpreter->ClearFileBusy();
      }
      if (fExitOnException == kExit)
         gSystem->Exit(128 + sig);
      else if (fExitOnException == kAbort)
         gSystem->Abort();
      else
         Throw(sig);
   }
   gSystem->Exit(128 + sig);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the exit on exception option. Setting this option determines what
/// happens in HandleException() in case an exception (kSigBus,
/// kSigSegmentationViolation, kSigIllegalInstruction or kSigFloatingException)
/// is trapped. Choices are: kDontExit (default), kExit or kAbort.
/// Returns the previous value.

TApplication::EExitOnException TApplication::ExitOnException(TApplication::EExitOnException opt)
{
   EExitOnException old = fExitOnException;
   fExitOnException = opt;
   return old;
}

////////////////////////////////////////////////////////////////////////////////
/// Static function used to create a default application environment.

void TApplication::CreateApplication()
{
   R__LOCKGUARD(gROOTMutex);
   // gApplication is set at the end of 'new TApplication.
   if (!gApplication) {
      char *a = StrDup("RootApp");
      char *b = StrDup("-b");
      char *argv[2];
      Int_t argc = 2;
      argv[0] = a;
      argv[1] = b;
      new TApplication("RootApp", &argc, argv, 0, 0);
      if (gDebug > 0)
         Printf("<TApplication::CreateApplication>: "
                "created default TApplication");
      delete [] a; delete [] b;
      gApplication->SetBit(kDefaultApplication);
   }
}

} // namespace CppyyLegacy

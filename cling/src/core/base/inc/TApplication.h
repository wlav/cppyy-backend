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
#include "TApplicationImp.h"


namespace CppyyLegacy {

class TObjArray;
class TSignalHandler;

class TApplication : public TObject {

public:
   // TApplication specific bits
   enum EStatusBits {
      kDefaultApplication = BIT(16)  // TRUE if created via CreateApplication()
   };
   // TApplication specific bits for fFiles
   enum EFileBits {
      kExpression = BIT(14)  // If the arguments is an expression rather than a file.
   };
   enum EExitOnException {
      kDontExit,
      kExit,
      kAbort
   };

private:
   Int_t              fArgc;            //Number of com   mand line arguments
   char             **fArgv;            //Command line arguments
   TApplicationImp   *fAppImp;          //!Window system specific application implementation
   TObjArray         *fFiles;           //Array of input files or C++ expression (TObjString's) specified via argv
   TString            fWorkDir;         //Working directory specified via argv
   TSignalHandler    *fSigHandler;      //Interrupt handler
   EExitOnException   fExitOnException; //Exit on exception option

   TApplication(const TApplication&);             // not implemented
   TApplication& operator=(const TApplication&);  // not implemented

protected:
   TApplication      *fAppRemote;      //Current remote application, if defined
   static TList      *fgApplications;  //List of available applications

   TApplication();

   void               SetSignalHandler(TSignalHandler *sh) { fSigHandler = sh; }

public:
   TApplication(const char *appClassName, Int_t *argc, char **argv,
                void *options = 0, Int_t numOptions = 0);
   virtual ~TApplication();

   TSignalHandler *GetSignalHandler() const { return fSigHandler; }
   virtual void    HandleException(Int_t sig);
   virtual Bool_t  HandleTermInput() { return kFALSE; }
   virtual void    Init() { fAppImp->Init(); }
   EExitOnException ExitOnException(EExitOnException opt = kExit);

   virtual const char *ApplicationName() const { return fAppImp->ApplicationName(); }

   virtual void    ls(Option_t *option="") const;

   TObjArray      *InputFiles() const { return fFiles; }
   const char     *WorkingDirectory() const { return fWorkDir; }
   void            ClearInputFiles();

   static TList   *GetApplications();
   static void     CreateApplication();

   ClassDef(TApplication,0)  //GUI application singleton
};

R__EXTERN TApplication *gApplication;

} // namespace CppyyLegacy

#endif

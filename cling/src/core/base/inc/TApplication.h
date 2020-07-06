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
   Bool_t             fIsRunning;       //True when in event loop (Run() has been called)
   Bool_t             fReturnFromRun;   //When true return from Run()
   Bool_t             fNoLog;           //Do not process logon and logoff macros
   Bool_t             fNoLogo;          //Do not show splash screen and welcome message
   Bool_t             fQuit;            //Exit after having processed input files
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
   virtual void    SetEchoMode(Bool_t mode);
   virtual void    HandleException(Int_t sig);
   virtual Bool_t  HandleTermInput() { return kFALSE; }
   virtual void    Init() { fAppImp->Init(); }
   virtual void    Run(Bool_t retrn = kFALSE);
   EExitOnException ExitOnException(EExitOnException opt = kExit);

   virtual const char *ApplicationName() const { return fAppImp->ApplicationName(); }
   virtual void    Show()    { fAppImp->Show(); }
   virtual void    Hide()    { fAppImp->Hide(); }
   virtual void    Iconify() { fAppImp->Iconify(); }
   virtual void    Open()    { fAppImp->Open(); }
   virtual void    Raise()   { fAppImp->Raise(); }
   virtual void    Lower()   { fAppImp->Lower(); }
   virtual Bool_t  IsCmdThread() { return fAppImp ? fAppImp->IsCmdThread() : kTRUE; }
   virtual TApplicationImp *GetApplicationImp() { return fAppImp;}

   virtual void    ls(Option_t *option="") const;

   Bool_t          NoLogOpt() const { return fNoLog; }
   Bool_t          NoLogoOpt() const { return fNoLogo; }
   Bool_t          QuitOpt() const { return fQuit; }
   TObjArray      *InputFiles() const { return fFiles; }
   const char     *WorkingDirectory() const { return fWorkDir; }
   void            ClearInputFiles();

   TApplication   *GetAppRemote() const { return fAppRemote; }

   Bool_t          IsRunning() const { return fIsRunning; }
   Bool_t          ReturnFromRun() const { return fReturnFromRun; }
   void            SetReturnFromRun(Bool_t ret) { fReturnFromRun = ret; }

   virtual void    LineProcessed(const char *line);   //*SIGNAL*
   virtual void    Terminate(Int_t status = 0);       //*SIGNAL*
   virtual void    KeyPressed(Int_t key);             //*SIGNAL*
   virtual void    ReturnPressed(char *text );        //*SIGNAL*

   static TList   *GetApplications();
   static void     CreateApplication();

   ClassDef(TApplication,0)  //GUI application singleton
};

R__EXTERN TApplication *gApplication;

} // namespace CppyyLegacy

#endif

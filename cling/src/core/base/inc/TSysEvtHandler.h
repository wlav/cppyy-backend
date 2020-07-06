// @(#)root/base:$Id$
// Author: Fons Rademakers   16/09/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TSysEvtHandler
#define ROOT_TSysEvtHandler


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TSysEvtHandler                                                       //
//                                                                      //
// Abstract base class for handling system events.                      //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TObject.h"

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TSignalHandler                                                       //
//                                                                      //
// Handles signals.                                                     //
//                                                                      //
//////////////////////////////////////////////////////////////////////////


namespace std { class exception; }

namespace CppyyLegacy {

enum ESignals {
   kSigBus,
   kSigSegmentationViolation,
   kSigSystem,
   kSigPipe,
   kSigIllegalInstruction,
   kSigAbort,
   kSigQuit,
   kSigInterrupt,
   kSigWindowChanged,
   kSigAlarm,
   kSigChild,
   kSigUrgent,
   kSigFloatingException,
   kSigTermination,
   kSigUser1,
   kSigUser2
};


class TSignalHandler : public TObject {

protected:
   ESignals    fSignal;   //Signal to be handled
   Bool_t      fSync;     //Synchronous or a-synchronous signal
   Int_t       fDelay;    //Delay handling of signal (use fDelay in Notify())
   Bool_t   fIsActive;    // kTRUE if handler is active, kFALSE if not active

   TSignalHandler(): fSignal((ESignals)-1), fSync(kTRUE), fDelay(0) { }

public:
   TSignalHandler(ESignals sig, Bool_t sync = kTRUE);
   virtual ~TSignalHandler() { Remove(); }
   void           Activate() { fIsActive = kTRUE; }
   void           DeActivate() { fIsActive = kFALSE; }
   Bool_t         IsActive() const { return fIsActive; }
   void           Delay() { fDelay = 1; }
   void           HandleDelayedSignal();
   ESignals       GetSignal() const { return fSignal; }
   void           SetSignal(ESignals sig) { fSignal = sig; }
   Bool_t         IsSync() const { return fSync; }
   Bool_t         IsAsync() const { return !fSync; }
   virtual Bool_t Notify();
   virtual void   Add();
   virtual void   Remove();

   ClassDef(TSignalHandler,0)  //Signal event handler
};

inline void TSignalHandler::HandleDelayedSignal()
{
   if (fDelay > 1) {
      fDelay = 0;
      Notify();
   } else
      fDelay = 0;
}


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TStdExceptionHandler                                                 //
//                                                                      //
// Handles standard C++ exceptions.                                     //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

class TStdExceptionHandler : public TObject {

public:
   enum EStatus { kSEProceed, kSEHandled, kSEAbort };

   TStdExceptionHandler();
   virtual ~TStdExceptionHandler() { }

   virtual void     Add();
   virtual void     Remove();
   virtual Bool_t   Notify();

   virtual EStatus  Handle(std::exception& exc) = 0;

   ClassDef(TStdExceptionHandler,0)  //C++ exception handler
};

} // namespace CppyyLegacy

#endif

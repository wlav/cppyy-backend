// @(#)root/base:$Id$
// Author: Fons Rademakers   16/09/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TSysEvtHandler
\ingroup Base

Abstract base class for handling system events.
*/

#include "TSysEvtHandler.h"
#include "TSystem.h"


ClassImp(CppyyLegacy::TSignalHandler);
ClassImp(CppyyLegacy::TStdExceptionHandler);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Create signal event handler.

TSignalHandler::TSignalHandler(ESignals sig, Bool_t sync)
{
   fSignal = sig;
   fSync   = sync;
   fDelay  = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Notify when signal occurs.

Bool_t TSignalHandler::Notify()
{
   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Add signal handler to system signal handler list.

void TSignalHandler::Add()
{
   if (gSystem && fSignal != (ESignals)-1) {
      gSystem->AddSignalHandler(this);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Remove signal handler from system signal handler list.

void TSignalHandler::Remove()
{
   if (gSystem && fSignal != (ESignals)-1) {
      gSystem->RemoveSignalHandler(this);
   }
}


////////////////////////////////////////////////////////////////////////////////
/// Handle standard C++ exceptions intercepted by the TSystem::Run().
///
/// Virtual method EStatus Handle(std::exception& exc) is called on the
/// collection of handlers registered to TSystem. The return value of
/// each handler influences the continuation of handling procedure:
///  - kSEProceed - Proceed with passing of the exception to other
///                 handlers, the exception has not been handled.
///  - kSEHandled - The exception has been handled, do not pass it to
///                 other handlers.
///  - kSEAbort   - Abort application.
/// If all handlers return kSEProceed TSystem::Run() rethrows the
/// exception, possibly resulting in process abortion.

TStdExceptionHandler::TStdExceptionHandler() {}

////////////////////////////////////////////////////////////////////////////////
/// Add std::exception handler to system handler list.

void TStdExceptionHandler::Add()
{
   if (gSystem) {
      gSystem->AddStdExceptionHandler(this);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Remove std::exception handler from system handler list.

void TStdExceptionHandler::Remove()
{
   if (gSystem) {
      gSystem->RemoveStdExceptionHandler(this);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Notify when signal occurs.

Bool_t TStdExceptionHandler::Notify()
{
   return kFALSE;
}

} // namespace CppyyLegacy

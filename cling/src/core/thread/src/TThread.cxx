// @(#)root/thread:$Id$
// Author: Fons Rademakers   02/07/97

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TThread                                                              //
//                                                                      //
// This class implements threads. A thread is an execution environment  //
// much lighter than a process. A single process can have multiple      //
// threads. The actual work is done via the TThreadImp class (either    //
// TPosixThread or TWin32Thread).                                       //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "RConfigure.h"

#include "TThread.h"
#include "TThreadImp.h"
#include "TThreadFactory.h"
#include "TROOT.h"
#include "TApplication.h"
#include "TMutex.h"
#include "TTimeStamp.h"
#include "TInterpreter.h"
#include "TError.h"
#include "Varargs.h"
#include "ThreadLocalStorage.h"
#include "TThreadSlots.h"
#include "TRWMutexImp.h"


namespace CppyyLegacy {

TThreadImp     *TThread::fgThreadImp = 0;
Long_t          TThread::fgMainId = 0;
TThread        *TThread::fgMain = 0;
TMutex         *TThread::fgMainMutex;

static void CINT_alloc_lock()   { gGlobalMutex->Lock(); }
static void CINT_alloc_unlock() { gGlobalMutex->UnLock(); }

static TMutex  *gMainInternalMutex = 0;

static void ThreadInternalLock() { if (gMainInternalMutex) gMainInternalMutex->Lock(); }
static void ThreadInternalUnLock() { if (gMainInternalMutex) gMainInternalMutex->UnLock(); }

static Bool_t fgIsTearDown(kFALSE);

extern "C" void ROOT_TThread_Initialize()
{
   TThread::Initialize();
};

//------------------------------------------------------------------------------

// Set gGlobalMutex to 0 when Thread library gets unloaded
class TThreadTearDownGuard {
public:
   TThreadTearDownGuard() { fgIsTearDown = kFALSE; }
   ~TThreadTearDownGuard() {
      // Note: we could insert here a wait for all thread to be finished.
      // this is questionable though as we need to balance between fixing a
      // user error (the thread was let lose and the caller did not explicit wait)
      // and the risk that we can not terminate a failing process.

      fgIsTearDown = kTRUE;
      TVirtualMutex *m = gGlobalMutex;
      gGlobalMutex = 0;
      delete m;
      TThreadImp *imp = TThread::fgThreadImp;
      TThread::fgThreadImp = 0;
      delete imp;
   }
};
static TThreadTearDownGuard gTearDownGuard;

//------------------------------------------------------------------------------

class TJoinHelper {
private:
   TThread    *fT;
   TThread    *fH;
   void      **fRet;
   Long_t      fRc;
   TMutex     *fM;
   TCondition *fC;
   Bool_t      fJoined;

   static void* JoinFunc(void *p);

public:
   TJoinHelper(TThread *th, void **ret);
   ~TJoinHelper();

   Int_t Join();
};

////////////////////////////////////////////////////////////////////////////////
/// Constructor of Thread helper class.

TJoinHelper::TJoinHelper(TThread *th, void **ret)
   : fT(th), fRet(ret), fRc(0), fM(new TMutex), fC(new TCondition(fM)), fJoined(kFALSE)
{
   fH = new TThread("JoinHelper", JoinFunc, this);
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor.

TJoinHelper::~TJoinHelper()
{
   delete fC;
   delete fM;
   delete fH;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method which runs in a separate thread to handle thread
/// joins without blocking the main thread.
/// Return a value (zero) so that it makes a joinable thread.

void* TJoinHelper::JoinFunc(void *p)
{
   TJoinHelper *jp = (TJoinHelper*)p;

   jp->fRc = jp->fT->Join(jp->fRet);

   jp->fM->Lock();
   jp->fJoined = kTRUE;
   jp->fC->Signal();
   jp->fM->UnLock();

   TThread::Exit(0);

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Thread join function.

Int_t TJoinHelper::Join()
{
   fM->Lock();
   fH->Run();

   while (kTRUE) {
      // TimedWaitRelative will release the mutex (i.e. equivalent to fM->Unlock),
      // then block on the condition variable.  Upon return it will lock the mutex.
      int r = fC->TimedWaitRelative(100);  // 100 ms

      // From the man page from pthread_ond_timedwait:

      // When using condition variables there is always a Boolean predicate
      // involving shared variables associated with each condition wait that
      // is true if the thread should proceed. Spurious wakeups from the
      // pthread_cond_timedwait() or pthread_cond_wait() functions may occur.
      // Since the return from pthread_cond_timedwait() or pthread_cond_wait()
      // does not imply anything about the value of this predicate, the
      // predicate should be re-evaluated upon such return.

      if (r == 0 || r == 1) {
         // If we received the signal or timed out, let's check the value
         if (fJoined) break;
      } else {
         // If any other error occured, there is no point in trying again
         break;
      }
   }

   fM->UnLock();

   // And wait for the help to finish to avoid the risk that it is still
   // running when the main tread is finished (and the thread library unloaded!)
   TThread::fgThreadImp->Join(fH, 0);

   return fRc;
}

} // namespace CppyyLegacy

//------------------------------------------------------------------------------
ClassImp(CppyyLegacy::TThread);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Create a thread. Specify the function or static class method
/// to be executed by the thread and a pointer to the argument structure.
/// The user function should return a void*. To start the thread call Run().

TThread::TThread(VoidRtnFunc_t fn, void *arg, EPriority pri)
   : TNamed("<anon>", "")
{
   fDetached  = kFALSE;
   fFcnVoid   = 0;
   fFcnRetn   = fn;
   fPriority  = pri;
   fThreadArg = arg;
   Constructor();
   fNamed     = kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Create a detached thread. Specify the function or static class method
/// to be executed by the thread and a pointer to the argument structure.
/// To start the thread call Run().

TThread::TThread(VoidFunc_t fn, void *arg, EPriority pri)
   : TNamed("<anon>", "")
{
   fDetached  = kTRUE;
   fFcnRetn   = 0;
   fFcnVoid   = fn;
   fPriority  = pri;
   fThreadArg = arg;
   Constructor();
   fNamed     = kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Create thread with a name. Specify the function or static class method
/// to be executed by the thread and a pointer to the argument structure.
/// The user function should return a void*. To start the thread call Run().

TThread::TThread(const char *thname, VoidRtnFunc_t fn, void *arg,
                 EPriority pri) : TNamed(thname, "")
{
   fDetached  = kFALSE;
   fFcnVoid   = 0;
   fFcnRetn   = fn;
   fPriority  = pri;
   fThreadArg = arg;
   Constructor();
   fNamed     = kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Create a detached thread with a name. Specify the function or static
/// class method to be executed by the thread and a pointer to the argument
/// structure. To start the thread call Run().

TThread::TThread(const char *thname, VoidFunc_t fn, void *arg,
                 EPriority pri) : TNamed(thname, "")
{
   fDetached  = kTRUE;
   fFcnRetn   = 0;
   fFcnVoid   = fn;
   fPriority  = pri;
   fThreadArg = arg;
   Constructor();
   fNamed     = kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Create a TThread for a already running thread.

TThread::TThread(Long_t id)
{
   fDetached  = kTRUE;
   fFcnRetn   = 0;
   fFcnVoid   = 0;
   fPriority  = kNormalPriority;
   fThreadArg = 0;
   Constructor();

   // Changing the id must be protected as it will be look at by multiple
   // threads (see TThread::GetThread)
   ThreadInternalLock();
   fNamed     = kFALSE;
   fId = (id ? id : SelfId());
   fState = kRunningState;
   ThreadInternalUnLock();

   if (gDebug)
      Info("TThread::TThread", "TThread attached to running thread");
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize the Thread package. This initializes the TThread and ROOT
/// global mutexes to make parts of ROOT thread safe/aware. This call is
/// implicit in case a TThread is created.

void TThread::Initialize()
{
   Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Return true, if the TThread objects have been initialize. If false,
/// the process is (from ROOT's point of view) single threaded.

Bool_t TThread::IsInitialized()
{
   if (fgThreadImp)
      return kTRUE;
   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize global state and variables once.

void TThread::Init()
{
   if (fgThreadImp || fgIsTearDown) return;

#if !defined (_REENTRANT) && !defined (WIN32)
   // Not having it means (See TVirtualMutext.h) that the LOCKGUARD macro are empty.
   ::CppyyLegacy::Fatal("Init","_REENTRANT must be #define-d for TThread to work properly.");
#endif

   // 'Insure' gROOT is created before initializing the Thread safe behavior
   // (to make sure we do not have two attempting to create it).
   CppyyLegacy::GetROOT();

   fgThreadImp = gThreadFactory->CreateThreadImp();
   gMainInternalMutex = new TMutex(kTRUE);

   fgMainId    = fgThreadImp->SelfId();
   fgMainMutex = new TMutex(kTRUE);
   gThreadTsd  = TThread::Tsd;

   // Create the single global mutex
   gGlobalMutex = new TMutex(kTRUE);
   // We need to make sure that gCling is initialized.
   TInterpreter::Instance()->SetAlloclockfunc(CINT_alloc_lock);
   gCling->SetAllocunlockfunc(CINT_alloc_unlock);

   // To avoid deadlocks, gInterpreterMutex and gROOTMutex need
   // to point at the same instance.
   // Both are now deprecated in favor of CppyyLegacy::gCoreMutex
   {
     R__LOCKGUARD(gGlobalMutex);
     if (!CppyyLegacy::gCoreMutex) {
        // To avoid dead locks, caused by shared library opening and/or static initialization
        // taking the same lock as 'tls_get_addr_tail', we can not use UniqueLockRecurseCount.
        CppyyLegacy::gCoreMutex = new CppyyLegacy::TRWMutexImp<std::mutex, CppyyLegacy::Internal::RecurseCounts>();
     }
     gInterpreterMutex = CppyyLegacy::gCoreMutex;
     gROOTMutex = gInterpreterMutex;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Common thread constructor.

void TThread::Constructor()
{
   fHolder = 0;
   fClean  = 0;
   fState  = kNewState;

   fId = -1;
   fHandle= 0;
   if (!fgThreadImp) Init();

   SetComment("Constructor: MainInternalMutex Locking");
   ThreadInternalLock();
   SetComment("Constructor: MainInternalMutex Locked");

   if (fgMain) fgMain->fPrev = this;
   fNext = fgMain; fPrev = 0; fgMain = this;

   ThreadInternalUnLock();
   SetComment();

   // thread is set up in initialisation routine or Run().
}

////////////////////////////////////////////////////////////////////////////////
/// Cleanup the thread.

TThread::~TThread()
{
   if (gDebug)
      Info("TThread::~TThread", "thread deleted");

   // Disconnect thread instance

   SetComment("Destructor: MainInternalMutex Locking");
   ThreadInternalLock();
   SetComment("Destructor: MainInternalMutex Locked");

   if (fPrev) fPrev->fNext = fNext;
   if (fNext) fNext->fPrev = fPrev;
   if (fgMain == this) fgMain = fNext;

   ThreadInternalUnLock();
   SetComment();
   if (fHolder) *fHolder = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to delete the specified thread.
/// Returns -1 in case the thread was running and has been killed. Returns
/// 0 in case the thread has been Delete and Cleaned up. The th pointer is
/// not valid anymore in that case.

Int_t TThread::Delete(TThread *&th)
{
   if (!th) return 0;
   th->fHolder = &th;

   if (th->fState == kRunningState) {     // Cancel if running
      th->fState = kDeletingState;

      if (gDebug)
         th->Info("TThread::Delete", "deleting thread");

      th->Kill();
      return -1;
   }

   CleanUp();
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to check if threads exist.
/// Returns the number of running threads.

Int_t TThread::Exists()
{
   ThreadInternalLock();

   Int_t num = 0;
   for (TThread *l = fgMain; l; l = l->fNext)
      num++; //count threads

   ThreadInternalUnLock();

   return num;
}

////////////////////////////////////////////////////////////////////////////////
/// Set thread priority.

void TThread::SetPriority(EPriority pri)
{
   fPriority = pri;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to find a thread by id.

TThread *TThread::GetThread(Long_t id)
{
   TThread *myTh;

   ThreadInternalLock();

   for (myTh = fgMain; myTh && (myTh->fId != id); myTh = myTh->fNext) { }

   ThreadInternalUnLock();

   return myTh;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to find a thread by name.

TThread *TThread::GetThread(const char *name)
{
   TThread *myTh;

   ThreadInternalLock();

   for (myTh = fgMain; myTh && (strcmp(name, myTh->GetName())); myTh = myTh->fNext) { }

   ThreadInternalUnLock();

   return myTh;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method returning pointer to current thread.

TThread *TThread::Self()
{
   TTHREAD_TLS(TThread*) self = 0;

   if (!self || fgIsTearDown) {
      if (fgIsTearDown) self = 0;
      self = GetThread(SelfId());
   }
   return self;
}


////////////////////////////////////////////////////////////////////////////////
/// Join this thread.

Long_t TThread::Join(void **ret)
{
   if (fId == -1) {
      Error("Join", "thread not running");
      return -1;
   }

   if (fDetached) {
      Error("Join", "cannot join detached thread");
      return -1;
   }

   if (SelfId() != fgMainId)
      return fgThreadImp->Join(this, ret);

   // do not block the main thread, use helper thread
   TJoinHelper helper(this, ret);

   return helper.Join();
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to join a thread by id.

Long_t TThread::Join(Long_t jid, void **ret)
{
   TThread *myTh = GetThread(jid);

   if (!myTh) {
      ::CppyyLegacy::Error("TThread::Join", "cannot find thread 0x%lx", jid);
      return -1L;
   }

   return myTh->Join(ret);
}

////////////////////////////////////////////////////////////////////////////////
/// Static method returning the id for the current thread.

Long_t TThread::SelfId()
{
   if (fgIsTearDown) return -1;
   if (!fgThreadImp) Init();

   return fgThreadImp->SelfId();
}

////////////////////////////////////////////////////////////////////////////////
/// Start the thread. This starts the static method TThread::Function()
/// which calls the user function specified in the TThread ctor with
/// the arg argument. Returns 0 on success, otherwise an error number will
/// be returned.

Int_t TThread::Run(void *arg)
{
   if (arg) fThreadArg = arg;

   SetComment("Run: MainInternalMutex locking");
   ThreadInternalLock();
   SetComment("Run: MainMutex locked");

   int iret = fgThreadImp->Run(this);

   fState = iret ? kInvalidState : kRunningState;

   if (gDebug)
      Info("TThread::Run", "thread run requested");

   ThreadInternalUnLock();
   SetComment();
   return iret;
}

////////////////////////////////////////////////////////////////////////////////
/// Kill this thread. Returns 0 on success, otherwise an error number will
/// be returned.

Int_t TThread::Kill()
{
   if (fState != kRunningState && fState != kDeletingState) {
      if (gDebug)
         Warning("TThread::Kill", "thread is not running");
      return 13;
   } else {
      if (fState == kRunningState ) fState = kCancelingState;
      return fgThreadImp->Kill(this);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to kill the thread by id. Returns 0 on success, otherwise
/// an error number will be returned.

Int_t TThread::Kill(Long_t id)
{
   TThread *th = GetThread(id);
   if (th) {
      return fgThreadImp->Kill(th);
   } else  {
      if (gDebug)
         ::CppyyLegacy::Warning("TThread::Kill(Long_t)", "thread 0x%lx not found", id);
      return 13;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to kill thread by name. Returns 0 on success, otherwise
/// an error number will be returned.

Int_t TThread::Kill(const char *name)
{
   TThread *th = GetThread(name);
   if (th) {
      return fgThreadImp->Kill(th);
   } else  {
      if (gDebug)
         ::CppyyLegacy::Warning("TThread::Kill(const char*)", "thread %s not found", name);
      return 13;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to turn off thread cancellation. Returns 0 on success,
/// otherwise an error number will be returned.

Int_t TThread::SetCancelOff()
{
   return fgThreadImp ? fgThreadImp->SetCancelOff() : -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to turn on thread cancellation. Returns 0 on success,
/// otherwise an error number will be returned.

Int_t TThread::SetCancelOn()
{
   return fgThreadImp ? fgThreadImp->SetCancelOn() : -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to set the cancellation response type of the calling thread
/// to asynchronous, i.e. cancel as soon as the cancellation request
/// is received.

Int_t TThread::SetCancelAsynchronous()
{
   return fgThreadImp ? fgThreadImp->SetCancelAsynchronous() : -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to set the cancellation response type of the calling thread
/// to deferred, i.e. cancel only at next cancellation point.
/// Returns 0 on success, otherwise an error number will be returned.

Int_t TThread::SetCancelDeferred()
{
   return fgThreadImp ? fgThreadImp->SetCancelDeferred() : -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to set a cancellation point. Returns 0 on success, otherwise
/// an error number will be returned.

Int_t TThread::CancelPoint()
{
   return fgThreadImp ? fgThreadImp->CancelPoint() : -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method which pushes thread cleanup method on stack.
/// Returns 0 in case of success and -1 in case of error.

Int_t TThread::CleanUpPush(void *free, void *arg)
{
   TThread *th = Self();
   if (th)
      return fgThreadImp->CleanUpPush(&(th->fClean), free, arg);
   return -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method which pops thread cleanup method off stack.
/// Returns 0 in case of success and -1 in case of error.

Int_t TThread::CleanUpPop(Int_t exe)
{
   TThread *th = Self();
   if (th)
      return fgThreadImp->CleanUpPop(&(th->fClean), exe);
   return -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to cleanup the calling thread.

Int_t TThread::CleanUp()
{
   TThread *th = Self();
   if (!th) return 13;

   fgThreadImp->CleanUp(&(th->fClean));
   fgMainMutex->CleanUp();

   gMainInternalMutex->CleanUp();

   if (th->fHolder)
      delete th;

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method which is called after the thread has been canceled.

void TThread::AfterCancel(TThread *th)
{
   if (th) {
      th->fState = kCanceledState;
      if (gDebug)
         th->Info("TThread::AfterCancel", "thread is canceled");
   } else
      ::CppyyLegacy::Error("TThread::AfterCancel", "zero thread pointer passed");
}

////////////////////////////////////////////////////////////////////////////////
/// Static method which terminates the execution of the calling thread.

Int_t TThread::Exit(void *ret)
{
   return fgThreadImp ? fgThreadImp->Exit(ret) : -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to get the current time. Returns
/// the number of seconds.

Int_t TThread::GetTime(ULong_t *absSec, ULong_t *absNanoSec)
{
   TTimeStamp t;
   if (absSec)     *absSec     = t.GetSec();
   if (absNanoSec) *absNanoSec = t.GetNanoSec();
   return t.GetSec();
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to lock the main thread mutex.

Int_t TThread::Lock()
{
   return (fgMainMutex ? fgMainMutex->Lock() : 0);
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to try to lock the main thread mutex.

Int_t TThread::TryLock()
{
   return (fgMainMutex ? fgMainMutex->TryLock() : 0);
}

////////////////////////////////////////////////////////////////////////////////
/// Static method to unlock the main thread mutex.

Int_t TThread::UnLock()
{
   return (fgMainMutex ? fgMainMutex->UnLock() : 0);
}

////////////////////////////////////////////////////////////////////////////////
/// Static method which is called by the system thread function and
/// which in turn calls the actual user function.

void *TThread::Function(void *ptr)
{
   TThread *th;
   void *ret, *arg;

   TThreadCleaner dummy;

   th = (TThread *)ptr;

   // Default cancel state is OFF
   // Default cancel type  is DEFERRED
   // User can change it by call SetCancelOn() and SetCancelAsynchronous()
   SetCancelOff();
   SetCancelDeferred();
   CleanUpPush((void *)&AfterCancel, th);  // Enable standard cancelling function

   if (gDebug)
      th->Info("TThread::Function", "thread is running");

   arg = th->fThreadArg;
   th->fState = kRunningState;

   if (th->fDetached) {
      //Detached, non joinable thread
      (th->fFcnVoid)(arg);
      ret = 0;
      th->fState = kFinishedState;
   } else {
      //UnDetached, joinable thread
      ret = (th->fFcnRetn)(arg);
      th->fState = kTerminatedState;
   }

   CleanUpPop(1);     // Disable standard canceling function

   if (gDebug)
      th->Info("TThread::Function", "thread has finished");

   TThread::Exit(ret);

   return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// Static method listing the existing threads.

void TThread::Ps()
{
   TThread *l;
   int i;

   if (!fgMain) {
      ::CppyyLegacy::Info("TThread::Ps", "no threads have been created");
      return;
   }

   ThreadInternalLock();

   int num = 0;
   for (l = fgMain; l; l = l->fNext)
      num++;

   char cbuf[256];
   printf("     Thread                   State\n");
   for (l = fgMain; l; l = l->fNext) { // loop over threads
      memset(cbuf, ' ', sizeof(cbuf));
      snprintf(cbuf, sizeof(cbuf), "%3d  %s:0x%lx", num--, l->GetName(), l->fId);
      i = strlen(cbuf);
      if (i < 30)
         cbuf[i] = ' ';
      cbuf[30] = 0;
      printf("%30s", cbuf);

      switch (l->fState) {   // print states
         case kNewState:        printf("Idle       "); break;
         case kRunningState:    printf("Running    "); break;
         case kTerminatedState: printf("Terminated "); break;
         case kFinishedState:   printf("Finished   "); break;
         case kCancelingState:  printf("Canceling  "); break;
         case kCanceledState:   printf("Canceled   "); break;
         case kDeletingState:   printf("Deleting   "); break;
         default:               printf("Invalid    ");
      }
      if (l->fComment[0]) printf("  // %s", l->fComment);
      printf("\n");
   }  // end of loop

   ThreadInternalUnLock();
}

////////////////////////////////////////////////////////////////////////////////
/// Static method returning a pointer to thread specific data container
/// of the calling thread.
/// k should be between 0 and kMaxUserThreadSlot for user application.
/// (and between kMaxUserThreadSlot and kMaxThreadSlot for ROOT libraries).
/// See CppyyLegacy::EThreadSlotReservation

void **TThread::Tsd(void *dflt, Int_t k)
{
   if (TThread::SelfId() == fgMainId) {   //Main thread
      return (void**)dflt;
   } else {
      return GetTls(k);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Static method that initializes the TLS array of a thread and returns the
/// reference to a given position in that array.

void **TThread::GetTls(Int_t k) {
   TTHREAD_TLS_ARRAY(void*, CppyyLegacy::kMaxThreadSlot, tls);

   // In order for the thread 'gDirectory' value to be properly
   // initialized we set it now (otherwise it defaults
   // to zero which is 'unexpected')
   // We initialize it to gROOT rather than gDirectory, since
   // TFile are currently expected to not be shared by two threads.
   if (k == CppyyLegacy::kDirectoryThreadSlot && tls[k] == nullptr)
      tls[k] = gROOT;

   return &(tls[k]);
}

////////////////////////////////////////////////////////////////////////////////
/// Static method providing a thread safe printf. Appends a newline.

void TThread::Printf(const char *va_(fmt), ...)
{
   va_list ap;
   va_start(ap,va_(fmt));

   Int_t buf_size = 2048;
   char *buf;

again:
   buf = new char[buf_size];

   int n = vsnprintf(buf, buf_size, va_(fmt), ap);
   // old vsnprintf's return -1 if string is truncated new ones return
   // total number of characters that would have been written
   if (n == -1 || n >= buf_size) {
      buf_size *= 2;
      delete [] buf;
      goto again;
   }

   va_end(ap);

   printf("%s\n", buf);
   fflush(stdout);

   delete [] buf;
}

////////////////////////////////////////////////////////////////////////////////
/// Thread specific error handler function.
/// It calls the user set error handler in the main thread.

void TThread::ErrorHandler(int level, const char *location, const char *fmt,
                           va_list ap) const
{
   Int_t buf_size = 2048;
   char *buf, *bp;

again:
   buf = new char[buf_size];

   int n = vsnprintf(buf, buf_size, fmt, ap);
   // old vsnprintf's return -1 if string is truncated new ones return
   // total number of characters that would have been written
   if (n == -1 || n >= buf_size) {
      buf_size *= 2;
      delete [] buf;
      goto again;
   }
   if (level >= kSysError && level < kFatal) {
      char *buf1 = new char[buf_size + strlen(gSystem->GetError()) + 5];
      sprintf(buf1, "%s (%s)", buf, gSystem->GetError());
      bp = buf1;
      delete [] buf;
   } else
      bp = buf;

   if (level != kFatal)
      ::CppyyLegacy::GetErrorHandler()(level, level >= gErrorAbortLevel, location, bp);
   else
      ::CppyyLegacy::GetErrorHandler()(level, kTRUE, location, bp);

   delete [] bp;
}

////////////////////////////////////////////////////////////////////////////////
/// Interface to ErrorHandler. User has to specify the class name as
/// part of the location, just like for the global Info(), Warning() and
/// Error() functions.

void TThread::DoError(int level, const char *location, const char *fmt,
                      va_list va) const
{
   char *loc = 0;

   if (location) {
      loc = new char[strlen(location) + strlen(GetName()) + 32];
      sprintf(loc, "%s %s:0x%lx", location, GetName(), fId);
   } else {
      loc = new char[strlen(GetName()) + 32];
      sprintf(loc, "%s:0x%lx", GetName(), fId);
   }

   ErrorHandler(level, loc, fmt, va);

   delete [] loc;
}

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  TThreadCleaner                                                      //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Call user clean up routines.

TThreadCleaner::~TThreadCleaner()
{
   TThread::CleanUp();
}

} // namespace CppyyLegacy

// @(#)root/base:$Id$
// Author: Rene Brun   08/12/94

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TROOT
\ingroup Base

ROOT top level object description.

The TROOT object is the entry point to the ROOT system.
The single instance of TROOT is accessible via the global gROOT.
Using the gROOT pointer one has access to basically every object
created in a ROOT based program. The TROOT object is essentially a
container of several lists pointing to the main ROOT objects.

The following lists are accessible from gROOT object:

~~~ {.cpp}
      gROOT->GetListOfClasses
      gROOT->GetListOfTypes
      gROOT->GetListOfGlobals
      gROOT->GetListOfGlobalFunctions
      gROOT->GetListOfFiles
      gROOT->GetListOfFunctions
      gROOT->GetListOfCleanups
      gROOT->GetListOfMessageHandlers
~~~

The TROOT class provides also many useful services:
  - Get pointer to an object in any of the lists above
  - Time utilities TROOT::Time

The ROOT object must be created as a static object. An example
of a main program creating an interactive version is shown below:

### Example of a main program

~~~ {.cpp}
      #include "TRint.h"

      int main(int argc, char **argv)
      {
         TRint *theApp = new TRint("ROOT example", &argc, argv);

         // Init Intrinsics, build all windows, and enter event loop
         theApp->Run();

         return(0);
      }
~~~
*/

#include <ROOT/RConfig.hxx>
#include "RConfigure.h"
#include "RConfigOptions.h"
#include "RVersion.h"

#include <string>
#include <map>
#include <stdlib.h>
#ifdef WIN32
#include <io.h>
#include "Windows4Root.h"
#include <Psapi.h>
#define RTLD_DEFAULT ((void *)::GetModuleHandle(NULL))
//#define dlsym(library, function_name) ::GetProcAddress((HMODULE)library, function_name)
#define dlopen(library_name, flags) ::LoadLibrary(library_name)
#define dlclose(library) ::FreeLibrary((HMODULE)library)
char *dlerror() {
   static char Msg[1000];
   FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), Msg,
                 sizeof(Msg), NULL);
   return Msg;
}
FARPROC dlsym(void *library, const char *function_name)
{
   HMODULE hMods[1024];
   DWORD cbNeeded;
   FARPROC address = NULL;
   unsigned int i;
   if (library == RTLD_DEFAULT) {
      if (EnumProcessModules(::GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
         for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            address = ::GetProcAddress((HMODULE)hMods[i], function_name);
            if (address)
               return address;
         }
      }
      return address;
   } else {
      return ::GetProcAddress((HMODULE)library, function_name);
   }
}
#else
#include <dlfcn.h>
#endif

#include "Riostream.h"
#include "ROOT/FoundationUtils.hxx"
#include "TROOT.h"
#include "TClass.h"
#include "TClassEdit.h"
#include "TClassGenerator.h"
#include "TDataType.h"
#include "TDatime.h"
#include "TObjectTable.h"
#include "TClassTable.h"
#include "TSystem.h"
#include "THashList.h"
#include "TObjArray.h"
#include "TEnv.h"
#include "TError.h"
#include "TGlobal.h"
#include "TFunction.h"
#include "TApplication.h"
#include "TInterpreter.h"
#include "TProcessUUID.h"
#include "TMap.h"
#include "TObjString.h"
#include "TVirtualMutex.h"
#include "TInterpreter.h"
#include "TListOfTypes.h"
#include "TListOfDataMembers.h"
#include "TListOfEnumsWithLock.h"
#include "TListOfFunctions.h"
#include "TListOfFunctionTemplates.h"
#include "TFunctionTemplate.h"
#include "ThreadLocalStorage.h"
#include "TVirtualRWMutex.h"

#include <string>
namespace std {} using namespace std;

#if defined(R__UNIX)
#include "TUnixSystem.h"
#elif defined(R__WIN32)
#include "TWinNTSystem.h"
#endif

extern "C" void R__SetZipMode(int);


namespace CppyyLegacy {

static DestroyInterpreter_t *gDestroyInterpreter = 0;
static void *gInterpreterLib = 0;

// Mutex for protection of concurrent gROOT access
TVirtualMutex* gROOTMutex = 0;
TVirtualRWMutex *gCoreMutex = nullptr;

// For accessing TThread::Tsd indirectly.
void **(*gThreadTsd)(void*,Int_t) = 0;

//-------- Names of next three routines are a small homage to CMZ --------------
////////////////////////////////////////////////////////////////////////////////
/// Return version id as an integer, i.e. "2.22/04" -> 22204.

static Int_t IVERSQ()
{
   Int_t maj, min, cycle;
   sscanf(ROOT_RELEASE, "%d.%d/%d", &maj, &min, &cycle);
   return 10000*maj + 100*min + cycle;
}

////////////////////////////////////////////////////////////////////////////////
/// Return built date as integer, i.e. "Apr 28 2000" -> 20000428.

static Int_t IDATQQ(const char *date)
{
   static const char *months[] = {"Jan","Feb","Mar","Apr","May",
                                  "Jun","Jul","Aug","Sep","Oct",
                                  "Nov","Dec"};

   char  sm[12];
   Int_t yy, mm=0, dd;
   sscanf(date, "%s %d %d", sm, &dd, &yy);
   for (int i = 0; i < 12; i++)
      if (!strncmp(sm, months[i], 3)) {
         mm = i+1;
         break;
      }
   return 10000*yy + 100*mm + dd;
}

////////////////////////////////////////////////////////////////////////////////
/// Return built time as integer (with min precision), i.e.
/// "17:32:37" -> 1732.

static Int_t ITIMQQ(const char *time)
{
   Int_t hh, mm, ss;
   sscanf(time, "%d:%d:%d", &hh, &mm, &ss);
   return 100*hh + mm;
}

////////////////////////////////////////////////////////////////////////////////
/// Clean up at program termination before global objects go out of scope.

static void CleanUpROOTAtExit()
{
   if (gROOT) {
      R__LOCKGUARD(gROOTMutex);

      if (gROOT->GetListOfFiles())
         gROOT->GetListOfFiles()->Delete("slow");
      if (gROOT->GetListOfClosedObjects())
         gROOT->GetListOfClosedObjects()->Delete("slow");
   }
}

////////////////////////////////////////////////////////////////////////////////
/// A module and its headers. Intentionally not a copy:
/// If these strings end up in this struct they are
/// long lived by definition because they get passed in
/// before initialization of TCling.

namespace {
   struct ModuleHeaderInfo_t {
      ModuleHeaderInfo_t(const char* moduleName,
                         const char** headers,
                         const char** includePaths,
                         const char* payloadCode,
                         const char* fwdDeclCode,
                         void (*triggerFunc)(),
                         const TROOT::FwdDeclArgsToKeepCollection_t& fwdDeclsArgToSkip,
                         const char **classesHeaders,
                         bool hasCxxModule):
                           fModuleName(moduleName),
                           fHeaders(headers),
                           fPayloadCode(payloadCode),
                           fFwdDeclCode(fwdDeclCode),
                           fIncludePaths(includePaths),
                           fTriggerFunc(triggerFunc),
                           fClassesHeaders(classesHeaders),
                           fFwdNargsToKeepColl(fwdDeclsArgToSkip),
                           fHasCxxModule(hasCxxModule) {}

      const char* fModuleName; // module name
      const char** fHeaders; // 0-terminated array of header files
      const char* fPayloadCode; // Additional code to be given to cling at library load
      const char* fFwdDeclCode; // Additional code to let cling know about selected classes and functions
      const char** fIncludePaths; // 0-terminated array of header files
      void (*fTriggerFunc)(); // Pointer to the dict initialization used to find the library name
      const char** fClassesHeaders; // 0-terminated list of classes and related header files
      const TROOT::FwdDeclArgsToKeepCollection_t fFwdNargsToKeepColl; // Collection of
                                                                      // pairs of template fwd decls and number of
      bool fHasCxxModule; // Whether this module has a C++ module alongside it.
   };

   std::vector<ModuleHeaderInfo_t>& GetModuleHeaderInfoBuffer() {
      static std::vector<ModuleHeaderInfo_t> moduleHeaderInfoBuffer;
      return moduleHeaderInfoBuffer;
   }
}

Bool_t TROOT::fgRootInit = kFALSE;

static void at_exit_of_TROOT() {
   if (Internal::gROOTLocal)
      Internal::gROOTLocal->~TROOT();
}

// This local static object initializes the ROOT system
namespace Internal {
   class TROOTAllocator {
      // Simple wrapper to separate, time-wise, the call to the
      // TROOT destructor and the actual free-ing of the memory.
      //
      // Since the interpreter implementation (currently TCling) is
      // loaded via dlopen by libCoreLegacy, the destruction of its global
      // variable (i.e. in particular clang's) is scheduled before
      // those in libCoreLegacy so we need to schedule the call to the TROOT
      // destructor before that *but* we want to make sure the memory
      // stay around until libCoreLegacy itself is unloaded so that code
      // using gROOT can 'properly' check for validity.
      //
      // The order of loading for is:
      //    libCoreLegacy.so
      //    ... anything other library hard linked to the executable ...
      //    ... for example libEvent
      //    libCling.so
      //    ... other libraries like libTree for example ....
      // and the destruction order is (of course) the reverse.
      // By default the unloading of the dictionary, does use
      // the service of the interpreter ... which of course
      // fails if libCling is already unloaded by that information
      // has not been registered per se.
      //
      // To solve this problem, we now schedule the destruction
      // of the TROOT object to happen _just_ before the
      // unloading/destruction of libCling so that we can
      // maximize the amount of clean-up we can do correctly
      // and we can still allocate the TROOT object's memory
      // statically.
      //
      char fHolder[sizeof(TROOT)];
   public:
      TROOTAllocator() {
         new(&(fHolder[0])) TROOT("root", "The ROOT of EVERYTHING");
      }

      ~TROOTAllocator() {
         if (gROOTLocal) {
            gROOTLocal->~TROOT();
         }
      }
   };

   // The global gROOT is defined to be a function (CppyyLegacy::GetROOT())
   // which itself is dereferencing a function pointer.

   // Initially this function pointer's value is & GetROOT1 whose role is to
   // create and initialize the TROOT object itself.
   // At the very end of the TROOT constructor the value of the function pointer
   // is switch to & GetROOT2 whose role is to initialize the interpreter.

   // This mechanism was primarily intended to fix the issues with order in which
   // global TROOT and LLVM globals are initialized. TROOT was initializing
   // Cling, but Cling could not be used yet due to LLVM globals not being
   // Initialized yet.  The solution is to delay initializing the interpreter in
   // TROOT till after main() when all LLVM globals are initialized.

   // Technically, the mechanism used actually delay the interpreter
   // initialization until the first use of gROOT *after* the end of the
   // TROOT constructor.

   // So to delay until after the start of main, we also made sure that none
   // of the ROOT code (mostly the dictionary code) used during library loading
   // is using gROOT (directly or indirectly).

   // In practice, the initialization of the interpreter is now delayed until
   // the first use gROOT (or gInterpreter) after the start of main (but user
   // could easily break this by using gROOT in their library initialization
   // code).

   extern TROOT *gROOTLocal;

   TROOT *GetROOT1() {
      if (gROOTLocal)
         return gROOTLocal;
      static TROOTAllocator alloc;
      return gROOTLocal;
   }

   TROOT *GetROOT2() {
      static Bool_t initInterpreter = kFALSE;
      if (!initInterpreter) {
         initInterpreter = kTRUE;
         gROOTLocal->InitInterpreter();
         // Load and init threads library
         gROOTLocal->InitThreads();
      }
      return gROOTLocal;
   }
   typedef TROOT *(*GetROOTFun_t)();

   static GetROOTFun_t gGetROOT = &GetROOT1;

   static Func_t GetSymInLibImt(const char *funcname)
   {
      const static bool loadSuccess = dlsym(RTLD_DEFAULT, "usedToIdentifyRootClingByDlSym")? false : 0 <= gSystem->Load("libImt");
      if (loadSuccess) {
         if (auto sym = gSystem->DynFindSymbol(nullptr, funcname)) {
            return sym;
         } else {
            Error("GetSymInLibImt", "Cannot get symbol %s.", funcname);
         }
      }
      return nullptr;
   }

} // end of Internal sub namespace
// back to CppyyLegacy namespace

   TROOT *GetROOT() {
      return (*Internal::gGetROOT)();
   }

   // clang-format off
   ////////////////////////////////////////////////////////////////////////////////
   /// Enables the global mutex to make ROOT thread safe/aware.
   ///
   /// The following becomes safe:
   /// - concurrent construction and destruction of TObjects, including the ones registered in ROOT's global lists (e.g. gROOT->GetListOfCleanups(), gROOT->GetListOfFiles())
   /// - concurrent calls to ROOT's type system classes, e.g. TClass and TEnum
   /// - concurrent calls to the interpreter through gInterpreter
   /// - concurrent loading of ROOT plug-ins
   ///
   /// The ROOT graphics subsystem is not made thread-safe by this method. In particular drawing or printing different
   /// canvases from different threads (and analogous operations such as invoking `Draw` on a `TObject`) is not thread-safe.
   ///
   /// Note that there is no `DisableThreadSafety()`. ROOT's thread-safety features cannot be disabled once activated.
   // clang-format on
   void EnableThreadSafety()
   {
      static void (*sym)() = (void(*)())Internal::GetSymInLibImt("ROOT_TThread_Initialize");
      if (sym)
         sym();
   }

TROOT *Internal::gROOTLocal = GetROOT();

// Global debug flag (set to > 0 to get debug output).
// Can be set either via the interpreter (gDebug is exported to CINT),
// via the rootrc resource "Root.Debug", via the shell environment variable
// ROOTDEBUG, or via the debugger.
Int_t gDebug;

} // namespace CppyyLegacy {

ClassImp(TROOT);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Default ctor.

TROOT::TROOT() : TDirectory(),
     fLineIsProcessing(0), fVersion(0), fVersionInt(0), fVersionCode(0),
     fVersionDate(0), fVersionTime(0), fApplication(0), fInterpreter(0),
     fMustClean(kTRUE),
     fClasses(0),fTypes(0),fGlobals(0),fGlobalFunctions(0),
     fClosedObjects(0),fFiles(0),fFunctions(0),
     fCleanups(0),
     fMessageHandlers(0),fStreamerInfo(0),fClassGenerators(0),
     fUUIDs(0)
{
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize the ROOT system. The creation of the TROOT object initializes
/// the ROOT system. It must be the first ROOT related action that is
/// performed by a program. The TROOT object must be created on the stack
/// (can not be called via new since "operator new" is protected). The
/// TROOT object is either created as a global object (outside the main()
/// program), or it is one of the first objects created in main().
/// Make sure that the TROOT object stays in scope for as long as ROOT
/// related actions are performed. TROOT is a so called singleton so
/// only one instance of it can be created. The single TROOT object can
/// always be accessed via the global pointer gROOT.
/// The name and title arguments can be used to identify the running
/// application. The initfunc argument can contain an array of
/// function pointers (last element must be 0). These functions are
/// executed at the end of the constructor. This way one can easily
/// extend the ROOT system without adding permanent dependencies
/// (e.g. the graphics system is initialized via such a function).

TROOT::TROOT(const char *name, const char *title, VoidFuncPtr_t *initfunc)
   : TDirectory(), fLineIsProcessing(0), fVersion(0), fVersionInt(0), fVersionCode(0),
     fVersionDate(0), fVersionTime(0), fApplication(0), fInterpreter(0),
     fMustClean(kTRUE),
     fClasses(0),fTypes(0),fGlobals(0),fGlobalFunctions(0),
     fClosedObjects(0),fFiles(0),fFunctions(0),
     fCleanups(0),
     fMessageHandlers(0),fStreamerInfo(0),fClassGenerators(0),
     fUUIDs(0)
{
   if (fgRootInit || Internal::gROOTLocal) {
      //Warning("TROOT", "only one instance of TROOT allowed");
      return;
   }

   R__LOCKGUARD(gROOTMutex);

   Internal::gROOTLocal = this;
   gDirectory = 0;

   SetName(name);
   SetTitle(title);

   // Initialize Operating System interface
   InitSystem();

   // Initialize static directory functions
   GetRootSys();
   GetBinDir();
   GetLibDir();
   GetIncludeDir();
   GetEtcDir();
   GetSourceDir();

   gRootDir = GetRootSys().Data();

   TDirectory::BuildDirectory(nullptr, nullptr);

   // Initialize interface to CINT C++ interpreter
   fVersionInt      = 0;  // check in TROOT dtor in case TCling fails
   fClasses         = 0;  // might be checked via TCling ctor
   fEnums           = 0;

   fVersion         = ROOT_RELEASE;
   fVersionCode     = ROOT_VERSION_CODE;
   fVersionInt      = IVERSQ();
   fVersionDate     = IDATQQ(ROOT_RELEASE_DATE);
   fVersionTime     = ITIMQQ(ROOT_RELEASE_TIME);

   fClasses         = new THashTable(800,3); fClasses->UseRWLock();
   //fIdMap           = new IdMap_t;
   fStreamerInfo    = new TObjArray(100); fStreamerInfo->UseRWLock();
   fClassGenerators = new TList;

   auto setNameLocked = [](TSeqCollection *l, const char *collection_name) {
      l->SetName(collection_name);
      l->UseRWLock();
      return l;
   };

   fApplication = 0;
   fTypes       = 0;
   fGlobals     = 0;
   fGlobalFunctions = 0;
   // fList was created in TDirectory::Build but with different sizing.
   delete fList;
   fList        = new THashList(1000,3); fList->UseRWLock();
   fClosedObjects = setNameLocked(new TList, "ClosedFiles");
   fFiles       = setNameLocked(new TList, "Files");
   fFunctions   = setNameLocked(new TList, "Functions");
   fCleanups    = setNameLocked(new THashList, "Cleanups");
   fMessageHandlers = setNameLocked(new TList, "MessageHandlers");
   fTypes       = new TListOfTypes; fTypes->UseRWLock();

   TProcessID::AddProcessID();
   fUUIDs = new TProcessUUID();

   // by default, add the list of files, tasks, canvases and browsers in the Cleanups list
   fCleanups->Add(fFiles);    fFiles->SetBit(kMustCleanup);
   fCleanups->Add(fClosedObjects); fClosedObjects->SetBit(kMustCleanup);
   // And add TROOT's TDirectory personality
   fCleanups->Add(fList);

   fMustClean     = kTRUE;
   fLineIsProcessing = 1;   // This prevents WIN32 "Windows" thread to pick ROOT objects with mouse
   gDirectory     = this;

   int i = 0;
   while (initfunc && initfunc[i]) {
      (initfunc[i])();
      i++;
   }

   // Set initial/default list of browsable objects
   atexit(CleanUpROOTAtExit);

   Internal::gGetROOT = &Internal::GetROOT2;
}

////////////////////////////////////////////////////////////////////////////////
/// Clean up and free resources used by ROOT (files, network sockets,
/// shared memory segments, etc.).

TROOT::~TROOT()
{
   using namespace Internal;

   if (gROOTLocal == this) {

      // If the interpreter has not yet been initialized, don't bother
      gGetROOT = &GetROOT1;

      // Mark the object as invalid, so that we can veto some actions
      // (like autoloading) while we are in the destructor.
      SetBit(TObject::kInvalidObject);

      // Turn-off the global mutex to avoid recreating mutexes that have
      // already been deleted during the destruction phase
      gGlobalMutex = 0;

      // Return when error occurred in TCling, i.e. when setup file(s) are
      // out of date
      if (!fVersionInt) return;

      // ATTENTION!!! Order is important!

      fClosedObjects->Delete("slow"); // and closed files
      fFiles->Delete("slow");       // and files
      SafeDelete(fFiles);

      SafeDelete(fClosedObjects);

      delete fUUIDs;
      TProcessID::Cleanup();                            // and list of ProcessIDs

      fFunctions->Delete();  SafeDelete(fFunctions);   // etc..

      // Stop emitting signals
      fMessageHandlers->Delete(); SafeDelete(fMessageHandlers);

      // Remove shared libraries produced by the TSystem::CompileMacro() call
      gSystem->CleanCompiledMacros();

      // Cleanup system class
      delete gSystem;

#ifndef _MSC_VER
      // deleting the interpreter makes things crash at exit in some cases
      delete fInterpreter;
#endif

      gROOTLocal = 0;
      fgRootInit = kFALSE;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Add a class to the list and map of classes.
/// This routine is deprecated, use TClass::AddClass directly.

void TROOT::AddClass(TClass *cl)
{
   TClass::AddClass(cl);
}

////////////////////////////////////////////////////////////////////////////////
/// Add a class generator.  This generator will be called by TClass::GetClass
/// in case its does not find a loaded rootcint dictionary to request the
/// creation of a TClass object.

void TROOT::AddClassGenerator(TClassGenerator *generator)
{
   if (!generator) return;
   fClassGenerators->Add(generator);
}

////////////////////////////////////////////////////////////////////////////////
/// Append object to this directory.
///
/// If replace is true:
///   remove any existing objects with the same same (if the name is not "")

void TROOT::Append(TObject *obj, Bool_t replace /* = kFALSE */)
{
   R__LOCKGUARD(gROOTMutex);
   TDirectory::Append(obj,replace);
}

namespace {
   static void R__ListSlowClose(TList *files)
   {
      // Routine to close a list of files using the 'slow' techniques
      // that also for the deletion ot update the list itself.

      static TObject harmless;
      TObjLink *cursor = files->FirstLink();
      while (cursor) {
         TDirectory *dir = static_cast<TDirectory*>( cursor->GetObject() );
         if (dir) {
            // In order for the iterator to stay valid, we must
            // prevent the removal of the object (dir) from the list
            // (which is done in TFile::Close).   We can also can not
            // just move to the next iterator since the Close might
            // also (indirectly) remove that file.
            // So we SetObject to a harmless value, so that 'dir'
            // is not seen as part of the list.
            // We will later, remove all the object (see files->Clear()
            cursor->SetObject(&harmless); // this must not be zero otherwise things go wrong.
            // See related comment at the files->Clear("nodelete");
            dir->Close("nodelete");
            // Put it back
            cursor->SetObject(dir);
         }
         cursor = cursor->Next();
      };
      // Now were done, clear the list but do not delete the objects as
      // they have been moved to the list of closed objects and must be
      // deleted from there in order to avoid a double delete from a
      // use objects (on the interpreter stack).
      files->Clear("nodelete");
   }

   static void R__ListSlowDeleteContent(TList *files)
   {
      // Routine to delete the content of list of files using the 'slow' techniques

      static TObject harmless;
      TObjLink *cursor = files->FirstLink();
      while (cursor) {
         TDirectory *dir = dynamic_cast<TDirectory*>( cursor->GetObject() );
         if (dir) {
            // In order for the iterator to stay valid, we must
            // prevent the removal of the object (dir) from the list
            // (which is done in TFile::Close).   We can also can not
            // just move to the next iterator since the Close might
            // also (indirectly) remove that file.
            // So we SetObject to a harmless value, so that 'dir'
            // is not seen as part of the list.
            // We will later, remove all the object (see files->Clear()
            cursor->SetObject(&harmless); // this must not be zero otherwise things go wrong.
            // See related comment at the files->Clear("nodelete");
            dir->GetList()->Delete("slow");
            // Put it back
            cursor->SetObject(dir);
         }
         cursor = cursor->Next();
      };
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Close any files and sockets that gROOT knows about.
/// This can be used to insures that the files and sockets are closed before any library is unloaded!

void TROOT::CloseFiles()
{
   if (fFiles && fFiles->First()) {
      R__ListSlowClose(static_cast<TList*>(fFiles));
   }
   // and Close TROOT itself.
   Close("slow");
}

////////////////////////////////////////////////////////////////////////////////
/// Execute the cleanups necessary at the end of the process, in particular
/// those that must be executed before the library start being unloaded.

void TROOT::EndOfProcessCleanups()
{
   // This will not delete the objects 'held' by the TFiles so that
   // they can still be 'reacheable' when ResetGlobals is run.
   CloseFiles();

   if (gInterpreter) {
      gInterpreter->ResetGlobals();
   }

   // Now delete the objects 'held' by the TFiles so that it
   // is done before the tear down of the libraries.
   if (fClosedObjects && fClosedObjects->First()) {
      R__ListSlowDeleteContent(static_cast<TList*>(fClosedObjects));
   }

   // Now a set of simpler things to delete.  See the same ordering in
   // TROOT::~TROOT
   fFunctions->Delete();

   if (gInterpreter) {
      gInterpreter->ShutDown();
   }
}


////////////////////////////////////////////////////////////////////////////////
/// Return pointer to type with name.

TDataType *TROOT::GetType(const char *name, Bool_t /* load */) const
{
   return (TDataType*)gROOT->GetListOfTypes()->FindObject(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to file with name.

TFile *TROOT::GetFile(const char *name) const
{
   R__LOCKGUARD(gROOTMutex);
   return (TFile*)GetListOfFiles()->FindObject(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to function with name.

TObject *TROOT::GetFunction(const char *name) const
{
   if (name == 0 || name[0] == 0) {
      return 0;
   }

   R__LOCKGUARD(gROOTMutex);
   return fFunctions->FindObject(name);
}

////////////////////////////////////////////////////////////////////////////////

TFunctionTemplate *TROOT::GetFunctionTemplate(const char *name)
{
   if (!gInterpreter) return 0;

   if (!fFuncTemplate) fFuncTemplate = new TListOfFunctionTemplates(0);

   return (TFunctionTemplate*)fFuncTemplate->FindObject(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to global variable by name. If load is true force
/// reading of all currently defined globals from CINT (more expensive).

TGlobal *TROOT::GetGlobal(const char *name, Bool_t load) const
{
   return (TGlobal *)gROOT->GetListOfGlobals(load)->FindObject(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to global variable with address addr.

TGlobal *TROOT::GetGlobal(const TObject *addr, Bool_t /* load */) const
{
   if (addr == 0 || ((intptr_t)addr) == -1) return 0;

   TInterpreter::DeclId_t decl = gInterpreter->GetDataMemberAtAddr(addr);
   if (decl) {
      TListOfDataMembers *globals = ((TListOfDataMembers*)(gROOT->GetListOfGlobals(kFALSE)));
      return (TGlobal*)globals->Get(decl);
   }
   // If we are actually looking for a global that is held by a global
   // pointer (for example gRandom), we need to find a pointer with the
   // correct value.
   decl = gInterpreter->GetDataMemberWithValue(addr);
   if (decl) {
      TListOfDataMembers *globals = ((TListOfDataMembers*)(gROOT->GetListOfGlobals(kFALSE)));
      return (TGlobal*)globals->Get(decl);
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Internal routine returning, and creating if necessary, the list
/// of global function.

TListOfFunctions *TROOT::GetGlobalFunctions()
{
   if (!fGlobalFunctions) fGlobalFunctions = new TListOfFunctions(0);
   return fGlobalFunctions;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the collection of functions named "name".

TCollection *TROOT::GetListOfFunctionOverloads(const char* name) const
{
   return ((TListOfFunctions*)fGlobalFunctions)->GetListForObject(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to global function by name.
/// If params != 0 it will also resolve overloading other it returns the first
/// name match.
/// If params == 0 and load is true force reading of all currently defined
/// global functions from Cling.
/// The param string must be of the form: "3189,\"aap\",1.3".

TFunction *TROOT::GetGlobalFunction(const char *function, const char *params,
                                    Bool_t load)
{
   if (!params) {
      R__LOCKGUARD(gROOTMutex);
      return (TFunction *)GetListOfGlobalFunctions(load)->FindObject(function);
   } else {
      if (!fInterpreter)
         Fatal("GetGlobalFunction", "fInterpreter not initialized");

      R__LOCKGUARD(gROOTMutex);
      TInterpreter::DeclId_t decl = gInterpreter->GetFunctionWithValues(0,
                                                                 function, params,
                                                                 false);

      if (!decl) return 0;

      TFunction *f = GetGlobalFunctions()->Get(decl);
      if (f) return f;

      Error("GetGlobalFunction",
            "\nDid not find matching TFunction <%s> with \"%s\".",
            function,params);
      return 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return pointer to global function by name. If proto != 0
/// it will also resolve overloading. If load is true force reading
/// of all currently defined global functions from CINT (more expensive).
/// The proto string must be of the form: "int, char*, float".

TFunction *TROOT::GetGlobalFunctionWithPrototype(const char *function,
                                               const char *proto, Bool_t load)
{
   if (!proto) {
      R__LOCKGUARD(gROOTMutex);
      return (TFunction *)GetListOfGlobalFunctions(load)->FindObject(function);
   } else {
      if (!fInterpreter)
         Fatal("GetGlobalFunctionWithPrototype", "fInterpreter not initialized");

      R__LOCKGUARD(gROOTMutex);
      TInterpreter::DeclId_t decl = gInterpreter->GetFunctionWithPrototype(0,
                                                                           function, proto);

      if (!decl) return 0;

      TFunction *f = GetGlobalFunctions()->Get(decl);
      if (f) return f;

      Error("GetGlobalFunctionWithPrototype",
            "\nDid not find matching TFunction <%s> with \"%s\".",
            function,proto);
      return 0;
   }
}

////////////////////////////////////////////////////////////////////////////////

TCollection *TROOT::GetListOfEnums(Bool_t load /* = kTRUE */)
{
   if(!fEnums.load()) {
      R__LOCKGUARD(gROOTMutex);
      // Test again just in case, another thread did the work while we were
      // waiting.
      if (!fEnums.load()) fEnums = new TListOfEnumsWithLock(0);
   }
   if (load) {
      R__LOCKGUARD(gROOTMutex);
      (*fEnums).Load(); // Refresh the list of enums.
   }
   return fEnums.load();
}

////////////////////////////////////////////////////////////////////////////////

TCollection *TROOT::GetListOfFunctionTemplates()
{
   R__LOCKGUARD(gROOTMutex);
   if(!fFuncTemplate) {
      fFuncTemplate = new TListOfFunctionTemplates(0);
   }
   return fFuncTemplate;
}

////////////////////////////////////////////////////////////////////////////////
/// Return list containing the TGlobals currently defined.
/// Since globals are created and deleted during execution of the
/// program, we need to update the list of globals every time we
/// execute this method. However, when calling this function in
/// a (tight) loop where no interpreter symbols will be created
/// you can set load=kFALSE (default).

TCollection *TROOT::GetListOfGlobals(Bool_t load)
{
   if (!fGlobals) {
      fGlobals = new TListOfDataMembers(0);
      // We add to the list the "funcky-fake" globals.

      // provide special functor for gROOT, while ROOT::GetROOT() does not return reference
      TGlobalMappedFunction::MakeFunctor("gROOT", "CppyyLegacy::TROOT*", GetROOT, [] {
         GetROOT();
         return (void *)&Internal::gROOTLocal;
      });

      TGlobalMappedFunction::MakeFunctor("gDirectory", "CppyyLegacy::TDirectory*", TDirectory::CurrentDirectory);

      // Don't let TGlobalMappedFunction delete our globals, now that we take them.
      fGlobals->AddAll(&TGlobalMappedFunction::GetEarlyRegisteredGlobals());
      TGlobalMappedFunction::GetEarlyRegisteredGlobals().SetOwner(kFALSE);
      TGlobalMappedFunction::GetEarlyRegisteredGlobals().Clear();
   }

   if (!fInterpreter)
      Fatal("GetListOfGlobals", "fInterpreter not initialized");

   if (load) fGlobals->Load();

   return fGlobals;
}

////////////////////////////////////////////////////////////////////////////////
/// Return list containing the TFunctions currently defined.
/// Since functions are created and deleted during execution of the
/// program, we need to update the list of functions every time we
/// execute this method. However, when calling this function in
/// a (tight) loop where no interpreter symbols will be created
/// you can set load=kFALSE (default).

TCollection *TROOT::GetListOfGlobalFunctions(Bool_t load)
{
   R__LOCKGUARD(gROOTMutex);

   if (!fGlobalFunctions) {
      fGlobalFunctions = new TListOfFunctions(0);
   }

   if (!fInterpreter)
      Fatal("GetListOfGlobalFunctions", "fInterpreter not initialized");

   // A thread that calls with load==true and a thread that calls with load==false
   // will conflict here (the load==true will be updating the list while the
   // other is reading it).  To solve the problem, we could use a read-write lock
   // inside the list itself.
   if (load) fGlobalFunctions->Load();

   return fGlobalFunctions;
}

////////////////////////////////////////////////////////////////////////////////
/// Return a dynamic list giving access to all TDataTypes (typedefs)
/// currently defined.
///
/// The list is populated on demand.  Calling
/// ~~~ {.cpp}
///    gROOT->GetListOfTypes()->FindObject(nameoftype);
/// ~~~
/// will return the TDataType corresponding to 'nameoftype'.  If the
/// TDataType is not already in the list itself and the type does exist,
/// a new TDataType will be created and added to the list.
///
/// list only the typedefs that have been previously accessed through the
/// list (plus the builtins types).

TCollection *TROOT::GetListOfTypes(Bool_t /* load */)
{
   if (!fInterpreter)
      Fatal("GetListOfTypes", "fInterpreter not initialized");

   return fTypes;
}


////////////////////////////////////////////////////////////////////////////////
/// Initialize operating system interface.

void TROOT::InitSystem()
{
   if (gSystem == 0) {
#if defined(R__UNIX)
      gSystem = new TUnixSystem;
#elif defined(R__WIN32)
      gSystem = new TWinNTSystem;
#else
      gSystem = new TSystem;
#endif

      if (gSystem->Init())
         fprintf(stderr, "Fatal in <TROOT::InitSystem>: can't init operating system layer\n");

      if (!gSystem->HomeDirectory()) {
         fprintf(stderr, "Fatal in <TROOT::InitSystem>: HOME directory not set\n");
         fprintf(stderr, "Fix this by defining the HOME shell variable\n");
      }

      // read default files
      gEnv = new TEnv(".rootrc");

      gDebug = gEnv->GetValue("Root.Debug", 0);

      if (!gEnv->GetValue("Root.ErrorHandlers", 1))
         gSystem->ResetSignals();

      // The old "Root.ZipMode" had a discrepancy between documentation vs actual meaning.
      // Also, a value with the meaning "default" wasn't available. To solved this,
      // "Root.ZipMode" was replaced by "Root.CompressionAlgorithm". Warn about usage of
      // the old value, if it's set to 0, but silently translate the setting to
      // "Root.CompressionAlgorithm" for values > 1.
      Int_t oldzipmode = gEnv->GetValue("Root.ZipMode", -1);
      if (oldzipmode == 0) {
         fprintf(stderr, "Warning in <TROOT::InitSystem>: ignoring old rootrc entry \"Root.ZipMode = 0\"!\n");
      } else {
         if (oldzipmode == -1 || oldzipmode == 1) {
            // Not set or default value, use "default" for "Root.CompressionAlgorithm":
            oldzipmode = 0;
         }
         // else keep the old zipmode (e.g. "3") as "Root.CompressionAlgorithm"
         // if "Root.CompressionAlgorithm" isn't set; see below.
      }

      Int_t zipmode = gEnv->GetValue("Root.CompressionAlgorithm", oldzipmode);
      if (zipmode != 0) R__SetZipMode(zipmode);

      const char *sdeb;
      if ((sdeb = gSystem->Getenv("ROOTDEBUG")))
         gDebug = atoi(sdeb);

      if (gDebug > 0 && isatty(2))
         fprintf(stderr, "Info in <TROOT::InitSystem>: running with gDebug = %d\n", gDebug);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Load and initialize thread library.

void TROOT::InitThreads()
{
   if (gEnv->GetValue("Root.UseThreads", 0) || gEnv->GetValue("Root.EnableThreadSafety", 0)) {
      EnableThreadSafety();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize the interpreter. Should be called only after main(),
/// to make sure LLVM/Clang is fully initialized.

void TROOT::InitInterpreter()
{
   // usedToIdentifyRootClingByDlSym is available when TROOT is part of
   // rootcling.
   if (!dlsym(RTLD_DEFAULT, "usedToIdentifyRootClingByDlSym")
       && !dlsym(RTLD_DEFAULT, "usedToIdentifyStaticRoot")) {
      // Make sure no llvm symbols are visible before loading libCling. If they
      // exist libCling will use those and not ours, causing havoc in the
      // interpreter. Look for an extern "C" symbol to avoid mangling; look for a
      // symbol from llvm because clang builds on top, so users would likely
      // have also their own llvm symbols when providing their own clang.
      void *LLVMEnablePrettyStackTraceAddr = 0;
      // Can't use gSystem->DynFindSymbol() because that iterates over all *known*
      // libraries which is not the same!
      LLVMEnablePrettyStackTraceAddr = dlsym(RTLD_DEFAULT, "LLVMEnablePrettyStackTrace");
      // FIXME: When we configure with -Dclingtest=On we intentionally export the symbols. Silence this error.
      if (LLVMEnablePrettyStackTraceAddr) {
         Error("InitInterpreter()", "LLVM SYMBOLS ARE EXPOSED TO CLING! "
               "This will cause problems; please hide them or dlopen() them "
               "after the call to TROOT::InitInterpreter()!");
      }

      char *libRIO = gSystem->DynamicPathName("libRIOLegacy");
      void *libRIOHandle = dlopen(libRIO, RTLD_NOW|RTLD_GLOBAL);
      delete [] libRIO;
      if (!libRIOHandle) {
         TString err = dlerror();
         fprintf(stderr, "Fatal in <TROOT::InitInterpreter>: cannot load library %s\n", err.Data());
         exit(1);
      }

      char *libcling = gSystem->DynamicPathName("libCling");
      gInterpreterLib = dlopen(libcling, RTLD_LAZY|RTLD_LOCAL);
      delete [] libcling;

      if (!gInterpreterLib) {
         TString err = dlerror();
         fprintf(stderr, "Fatal in <TROOT::InitInterpreter>: cannot load library %s\n", err.Data());
         exit(1);
      }
      dlerror();   // reset error message
   } else {
      gInterpreterLib = RTLD_DEFAULT;
   }
   CreateInterpreter_t *CreateInterpreter = (CreateInterpreter_t*) dlsym(gInterpreterLib, "CreateInterpreter");
   if (!CreateInterpreter) {
      TString err = dlerror();
      fprintf(stderr, "Fatal in <TROOT::InitInterpreter>: cannot load symbol %s\n", err.Data());
      exit(1);
   }
   // Schedule the destruction of TROOT.
   atexit(at_exit_of_TROOT);

   gDestroyInterpreter = (DestroyInterpreter_t*) dlsym(gInterpreterLib, "DestroyInterpreter");
   if (!gDestroyInterpreter) {
      TString err = dlerror();
      fprintf(stderr, "Fatal in <TROOT::InitInterpreter>: cannot load symbol %s\n", err.Data());
      exit(1);
   }

   const char *interpArgs[] = {
#ifdef NDEBUG
      "-DNDEBUG",
#else
      "-UNDEBUG",
#endif
#ifdef DEBUG
      "-DDEBUG",
#else
      "-UDEBUG",
#endif
#ifdef _DEBUG
      "-D_DEBUG",
#else
      "-U_DEBUG",
#endif
      nullptr};

   fInterpreter = CreateInterpreter(gInterpreterLib, interpArgs);

   fCleanups->Add(fInterpreter);
   fInterpreter->SetBit(kMustCleanup);

   fgRootInit = kTRUE;

   // initialize gClassTable is not already done
   if (!gClassTable)
      new TClassTable;

   // Initialize all registered dictionaries.
   for (std::vector<ModuleHeaderInfo_t>::const_iterator
           li = GetModuleHeaderInfoBuffer().begin(),
           le = GetModuleHeaderInfoBuffer().end(); li != le; ++li) {
         // process buffered module registrations
      fInterpreter->RegisterModule(li->fModuleName,
                                   li->fHeaders,
                                   li->fIncludePaths,
                                   li->fPayloadCode,
                                   li->fFwdDeclCode,
                                   li->fTriggerFunc,
                                   li->fFwdNargsToKeepColl,
                                   li->fClassesHeaders,
                                   kTRUE /*lateRegistration*/,
                                   li->fHasCxxModule);
   }
   GetModuleHeaderInfoBuffer().clear();

   fInterpreter->Initialize();
}

////////////////////////////////////////////////////////////////////////////////
/// Helper function used by TClass::GetClass().
/// This function attempts to load the dictionary for 'classname'
/// either from the TClassTable or from the list of generator.
/// If silent is 'true', do not warn about missing dictionary for the class.
/// (typically used for class that are used only for transient members)
///
/// The 'requestedname' is expected to be already normalized.

TClass *TROOT::LoadClass(const char *requestedname, Bool_t silent) const
{
   return TClass::LoadClass(requestedname, silent);
}

////////////////////////////////////////////////////////////////////////////////
/// Check if class "classname" is known to the interpreter (in fact,
/// this check is not needed anymore, so classname is ignored). If
/// not it will load library "libname". If the library couldn't be found with original
/// libname and if the name was not prefixed with lib, try to prefix with "lib" and search again.
/// If DynamicPathName still couldn't find the library, return -1.
/// If check is true it will only check if libname exists and is
/// readable.
/// Returns 0 on successful loading, -1 in case libname does not
/// exist or in case of error and -2 in case of version mismatch.

Int_t TROOT::LoadClass(const char * /*classname*/, const char *libname,
                       Bool_t check)
{
   TString lib(libname);

   // Check if libname exists in path or not
   if (char *path = gSystem->DynamicPathName(lib, kTRUE)) {
      // If check == true, only check if it exists and if it's readable
      if (check) {
         delete [] path;
         return 0;
      }

      // If check == false, try to load the library
      else {
         int err = gSystem->Load(path, 0, kTRUE);
         delete [] path;

         // TSystem::Load returns 1 when the library was already loaded, return success in this case.
         if (err == 1)
            err = 0;
         return err;
      }
   } else {
      // This is the branch where libname didn't exist
      if (check) {
         FileStat_t stat;
         if (!gSystem->GetPathInfo(libname, stat) && (R_ISREG(stat.fMode) &&
             !gSystem->AccessPathName(libname, kReadPermission)))
            return 0;
      }

      // Take care of user who didn't write the whole name
      if (!lib.BeginsWith("lib")) {
         lib = "lib" + lib;
         return LoadClass("", lib.Data(), check);
      }
   }

   // Execution reaches here when library was prefixed with lib, check is false and couldn't find
   // the library name.
   return -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if the file is local and is (likely) to be a ROOT file

Bool_t TROOT::IsRootFile(const char *filename) const
{
   Bool_t result = kFALSE;
   FILE *mayberootfile = fopen(filename,"rb");
   if (mayberootfile) {
      char header[5];
      if (fgets(header,5,mayberootfile)) {
         result = strncmp(header,"root",4)==0;
      }
      fclose(mayberootfile);
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
/// Recursively remove this object from the list of Cleanups.
/// Typically RecursiveRemove is implemented by classes that can contain
/// mulitple references to a same object or shared ownership of the object
/// with others.

void TROOT::RecursiveRemove(TObject *obj)
{
   R__READ_LOCKGUARD(gCoreMutex);

   fCleanups->RecursiveRemove(obj);
}

////////////////////////////////////////////////////////////////////////////////
/// Insure that the files, canvases and sockets are closed.

static void CallCloseFiles()
{
   if (TROOT::Initialized() && Internal::gROOTLocal) {
      gROOT->CloseFiles();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Called by static dictionary initialization to register clang modules
/// for headers. Calls TCling::RegisterModule() unless gCling
/// is NULL, i.e. during startup, where the information is buffered in
/// the static GetModuleHeaderInfoBuffer().

void TROOT::RegisterModule(const char* modulename,
                           const char** headers,
                           const char** includePaths,
                           const char* payloadCode,
                           const char* fwdDeclCode,
                           void (*triggerFunc)(),
                           const TInterpreter::FwdDeclArgsToKeepCollection_t& fwdDeclsArgToSkip,
                           const char** classesHeaders,
                           bool hasCxxModule)
{

   // First a side track to insure proper end of process behavior.

   // Register for each loaded dictionary (and thus for each library),
   // that we need to Close the ROOT files as soon as this library
   // might start being unloaded after main.
   //
   // By calling atexit here (rather than directly from within the
   // library) we make sure that this is not called if the library is
   // 'only' dlclosed.

   // Similarly, the ordering (before this commit) was broken in the
   // following case:

   //    TApplication creation (EndOfProcessCleanups registration)
   //    load UserLibrary
   //    create TFile
   //    Append UserObject to TFile

   // and after the end of main the order of execution was

   //    unload UserLibrary
   //    call EndOfProcessCleanups
   //       Write the TFile
   //         attempt to write the user object.
   //    ....

   // where what we need is to have the files closen/written before
   // the unloading of the library.

   // To solve the problem we now register an atexit function for
   // every dictionary thus making sure there is at least one executed
   // before the first library tear down after main.

   // If atexit is called directly within a library's code, the
   // function will called *either* when the library is 'dlclose'd or
   // after then end of main (whichever comes first).  We do *not*
   // want the files to be closed whenever a library is unloaded via
   // dlclose.  To avoid this, we add the function (CallCloseFiles)
   // from the dictionary indirectly (via RegisterModule).  In
   // this case the function will only only be called either when
   // libCoreLegacy is 'dlclose'd or right after the end of main.

   atexit(CallCloseFiles);

   // Now register with TCling.
   if (TROOT::Initialized()) {
      gCling->RegisterModule(modulename, headers, includePaths, payloadCode, fwdDeclCode, triggerFunc,
                             fwdDeclsArgToSkip, classesHeaders, false, hasCxxModule);
   } else {
      GetModuleHeaderInfoBuffer().push_back(ModuleHeaderInfo_t(modulename, headers, includePaths, payloadCode,
                                                               fwdDeclCode, triggerFunc, fwdDeclsArgToSkip,
                                                               classesHeaders, hasCxxModule));
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Remove an object from the in-memory list.
///    Since TROOT is global resource, this is lock protected.

TObject *TROOT::Remove(TObject* obj)
{
   R__LOCKGUARD(gROOTMutex);
   return TDirectory::Remove(obj);
}

////////////////////////////////////////////////////////////////////////////////
/// Remove a class from the list and map of classes.
/// This routine is deprecated, use TClass::RemoveClass directly.

void TROOT::RemoveClass(TClass *oldcl)
{
   TClass::RemoveClass(oldcl);
}

////////////////////////////////////////////////////////////////////////////////
/// Delete all global interpreter objects created since the last call to Reset
///
/// If option="a" is set reset to startup context (i.e. unload also
/// all loaded files, classes, structs, typedefs, etc.).
///
/// This function is typically used at the beginning (or end) of an unnamed macro
/// to clean the environment.
///
/// IMPORTANT WARNING:
/// Do not use this call from within any function (neither compiled nor
/// interpreted.  This should only be used from a unnamed macro
/// (which starts with a { (curly braces)  ).  For example, using TROOT::Reset
/// from within an interpreted function will lead to the unloading of the
/// dictionary and source file, including the one defining the function being
/// executed.
///

void TROOT::Reset(Option_t *option)
{
   if (fInterpreter) {
      if (!strncmp(option, "a", 1)) {
         fInterpreter->Reset();
         fInterpreter->SaveContext();
      } else
         gInterpreter->ResetGlobals();

      if (fGlobals) fGlobals->Unload();
      if (fGlobalFunctions) fGlobalFunctions->Unload();

      SaveContext();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Save the current interpreter context.

void TROOT::SaveContext()
{
   if (fInterpreter)
      gInterpreter->SaveGlobalsContext();
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize ROOT explicitly.

void TROOT::Initialize() {
   (void) gROOT;
}

////////////////////////////////////////////////////////////////////////////////
/// Return kTRUE if the TROOT object has been initialized.

Bool_t TROOT::Initialized()
{
   return fgRootInit;
}

////////////////////////////////////////////////////////////////////////////////
/// Convert version code to an integer, i.e. 331527 -> 51507.

Int_t TROOT::ConvertVersionCode2Int(Int_t code)
{
   return 10000*(code>>16) + 100*((code&65280)>>8) + (code&255);
}

////////////////////////////////////////////////////////////////////////////////
/// Return ROOT version code as defined in RVersion.h.

Int_t TROOT::RootVersionCode()
{
   return ROOT_VERSION_CODE;
}

////////////////////////////////////////////////////////////////////////////////
/// Provide command line arguments to the interpreter construction.
/// These arguments are added to the existing flags (e.g. `-DNDEBUG`).
/// They are evaluated once per process, at the time where TROOT (and thus
/// TInterpreter) is constructed.
/// Returns the new flags.

const std::vector<std::string> &TROOT::AddExtraInterpreterArgs(const std::vector<std::string> &args) {
   static std::vector<std::string> sArgs = {};
   sArgs.insert(sArgs.begin(), args.begin(), args.end());
   return sArgs;
}

////////////////////////////////////////////////////////////////////////////////
/// INTERNAL function!
/// Used by rootcling to inject interpreter arguments through a C-interface layer.

const char**& TROOT::GetExtraInterpreterArgs() {
   static const char** extraInterpArgs = 0;
   return extraInterpArgs;
}

////////////////////////////////////////////////////////////////////////////////

#ifdef ROOTPREFIX
static Bool_t IgnorePrefix() {
   static Bool_t ignorePrefix = gSystem->Getenv("ROOTIGNOREPREFIX");
   return ignorePrefix;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// Get the rootsys directory in the installation. Static utility function.

const TString& TROOT::GetRootSys() {
   // Avoid returning a reference to a temporary because of the conversion
   // between std::string and TString.
   const static TString rootsys = FoundationUtils::GetRootSys();
   return rootsys;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the binary directory in the installation. Static utility function.

const TString& TROOT::GetBinDir() {
#ifdef ROOTBINDIR
   if (IgnorePrefix()) {
#endif
      static TString rootbindir;
      if (rootbindir.IsNull()) {
         rootbindir = "bin";
         gSystem->PrependPathName(GetRootSys(), rootbindir);
      }
      return rootbindir;
#ifdef ROOTBINDIR
   } else {
      const static TString rootbindir = ROOTBINDIR;
      return rootbindir;
   }
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Get the library directory in the installation. Static utility function.

const TString& TROOT::GetLibDir() {
#ifdef ROOTLIBDIR
   if (IgnorePrefix()) {
#endif
      static TString rootlibdir;
      if (rootlibdir.IsNull()) {
         rootlibdir = "lib";
         gSystem->PrependPathName(GetRootSys(), rootlibdir);
      }
      return rootlibdir;
#ifdef ROOTLIBDIR
   } else {
      const static TString rootlibdir = ROOTLIBDIR;
      return rootlibdir;
   }
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Get the include directory in the installation. Static utility function.

const TString& TROOT::GetIncludeDir() {
   // Avoid returning a reference to a temporary because of the conversion
   // between std::string and TString.
   const static TString includedir = FoundationUtils::GetIncludeDir();
   return includedir;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the sysconfig directory in the installation. Static utility function.

const TString& TROOT::GetEtcDir() {
   // Avoid returning a reference to a temporary because of the conversion
   // between std::string and TString.
   const static TString etcdir = FoundationUtils::GetEtcDir();
   return etcdir;
}

////////////////////////////////////////////////////////////////////////////////
/// Shut down ROOT.

void TROOT::ShutDown()
{
   if (gROOT)
      gROOT->EndOfProcessCleanups();
   else if (gInterpreter)
      gInterpreter->ShutDown();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the source directory in the installation. Static utility function.

const TString& TROOT::GetSourceDir() {
#ifdef ROOTSRCDIR
   if (IgnorePrefix()) {
#endif
      static TString rootsrcdir;
      if (rootsrcdir.IsNull()) {
         rootsrcdir = "src";
         gSystem->PrependPathName(GetRootSys(), rootsrcdir);
      }
      return rootsrcdir;
#ifdef ROOTSRCDIR
   } else {
      const static TString rootsrcdir = ROOTSRCDIR;
      return rootsrcdir;
   }
#endif
}

} // namespace CppyyLegacy

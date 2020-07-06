// @(#)root/base:$Id$
// Author: Rene Brun   08/12/94

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TROOT
#define ROOT_TROOT


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TROOT                                                                //
//                                                                      //
// The TROOT object is the entry point to the system.                   //
// The single instance of TROOT is accessible via the global gROOT.     //
// Using the gROOT pointer one has access to basically every object     //
// created in a ROOT based program. The TROOT object is essentially a   //
// "dispatcher" with several lists pointing to the ROOT main objects.   //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TDirectory.h"
#include "TList.h"
#include "RConfigure.h"

#include <atomic>
#include <string>
#include <vector>


namespace CppyyLegacy {

class TClass;
class TDataType;
class TFile;
class TApplication;
class TInterpreter;
class TGlobal;
class TFunction;
class TFolder;
class TProcessUUID;
class TClassGenerator;
class TVirtualMutex;
class TROOT;
class TListOfDataMembers;
class TListOfEnums;
class TListOfFunctions;
class TListOfFunctionTemplates;
class TFunctionTemplate;
class TGlobalMappedFunction;

R__EXTERN TVirtualMutex *gROOTMutex;

namespace Internal {
   class TROOTAllocator;

   TROOT *GetROOT2();

} // namespace Internal

// Enable support for multi-threading within the ROOT code,
// in particular, enables the global mutex to make ROOT thread safe/aware.
void EnableThreadSafety();
/// \brief Enable ROOT's implicit multi-threading for all objects and methods that provide an internal
/// parallelisation mechanism.

class TROOT : public TDirectory {

friend class TCling;
friend TROOT *CppyyLegacy::Internal::GetROOT2();

private:
   Int_t           fLineIsProcessing;     //To synchronize multi-threads

   static Int_t    fgDirLevel;            //Indentation level for ls()
   static Bool_t   fgRootInit;            //Singleton initialization flag

   TROOT(const TROOT&);                   //Not implemented
   TROOT& operator=(const TROOT&);        //Not implemented

protected:
   typedef std::atomic<TListOfEnums*> AListOfEnums_t;

   TString         fConfigOptions;        //ROOT ./configure set build options
   TString         fConfigFeatures;       //ROOT ./configure detected build features
   TString         fVersion;              //ROOT version (from CMZ VERSQQ) ex 0.05/01
   Int_t           fVersionInt;           //ROOT version in integer format (501)
   Int_t           fVersionCode;          //ROOT version code as used in RVersion.h
   Int_t           fVersionDate;          //Date of ROOT version (ex 951226)
   Int_t           fVersionTime;          //Time of ROOT version (ex 1152)
   Int_t           fTimer;                //Timer flag
   std::atomic<TApplication*> fApplication;  //Pointer to current application
   TInterpreter    *fInterpreter;         //Command interpreter
   Bool_t          fBatch;                //True if session without graphics
   Bool_t          fMustClean;            //True if object destructor scans canvases
   Bool_t          fReadingObject;        //True while reading an object [Deprecated (will be removed in next release)
   TCollection     *fClasses;             //List of classes definition
   TCollection     *fTypes;               //List of data types definition
   TListOfFunctionTemplates *fFuncTemplate; //List of global function templates
   TListOfDataMembers*fGlobals;             //List of global variables
   TListOfFunctions*fGlobalFunctions;     //List of global functions
   TSeqCollection  *fClosedObjects;       //List of closed objects from the list of files and sockets, so we can delete them if neededCl.
   TSeqCollection  *fFiles;               //List of files
   TSeqCollection  *fMappedFiles;         //List of memory mapped files
   TCollection     *fFunctions;           //List of analytic functions
   TSeqCollection  *fCleanups;            //List of recursiveRemove collections
   TSeqCollection  *fMessageHandlers;     //List of message handlers
   TSeqCollection  *fStreamerInfo;        //List of active StreamerInfo classes
   TCollection     *fClassGenerators;     //List of user defined class generators;
   AListOfEnums_t   fEnums;               //List of enum types
   TProcessUUID    *fUUIDs;               //Pointer to TProcessID managing TUUIDs
   TFolder         *fRootFolder;          //top level folder //root

                  TROOT();                //Only used by Dictionary
   void           InitSystem();           //Operating System interface
   void           InitThreads();          //Initialize threads library
   void           InitInterpreter();      //Initialize interpreter (cling)
   void          *operator new(size_t l) { return TObject::operator new(l); }
   void          *operator new(size_t l, void *ptr) { return TObject::operator new(l,ptr); }

   friend class CppyyLegacy::Internal::TROOTAllocator;

   TListOfFunctions*GetGlobalFunctions();

public:

   typedef std::vector<std::pair<std::string, int> > FwdDeclArgsToKeepCollection_t;

                     TROOT(const char *name, const char *title, VoidFuncPtr_t *initfunc = 0);
   virtual           ~TROOT();
   void              AddClass(TClass *cl);
   void              AddClassGenerator(TClassGenerator *gen);
   virtual void      Append(TObject *obj, Bool_t replace = kFALSE);
   Bool_t            ClassSaved(TClass *cl);
   void              CloseFiles();
   void              EndOfProcessCleanups();
   virtual TObject  *FindObject(const char *name) const;
   virtual TObject  *FindObject(const TObject *obj) const;
   virtual TObject  *FindObjectAny(const char *name) const;
   virtual TObject  *FindObjectAnyFile(const char *name) const;
   const char       *FindObjectClassName(const char *name) const;
   const char       *FindObjectPathName(const TObject *obj) const;
   TClass           *FindSTLClass(const char *name, Bool_t load, Bool_t silent = kFALSE) const;
   TApplication     *GetApplication() const { return fApplication; }
   TInterpreter     *GetInterpreter() const { return fInterpreter; }
   TClass           *GetClass(const char *name, Bool_t load = kTRUE, Bool_t silent = kFALSE) const;
   TClass           *GetClass(const std::type_info &typeinfo, Bool_t load = kTRUE, Bool_t silent = kFALSE) const;
   const char       *GetConfigOptions() const { return fConfigOptions; }
   const char       *GetConfigFeatures() const { return fConfigFeatures; }
   Int_t             GetVersionDate() const { return fVersionDate; }
   Int_t             GetVersionTime() const { return fVersionTime; }
   Int_t             GetVersionInt() const { return fVersionInt; }
   Int_t             GetVersionCode() const { return fVersionCode; }
   const char       *GetVersion() const { return fVersion; }
   TCollection      *GetListOfClasses() const { return fClasses; }
   TCollection      *GetListOfTypes(Bool_t load = kFALSE);
   TCollection      *GetListOfGlobals(Bool_t load = kFALSE);
   TCollection      *GetListOfGlobalFunctions(Bool_t load = kFALSE);
   TSeqCollection   *GetListOfClosedObjects() const { return fClosedObjects; }
   TSeqCollection   *GetListOfFiles() const       { return fFiles; }
   TSeqCollection   *GetListOfMappedFiles() const { return fMappedFiles; }
   TCollection      *GetListOfFunctions() const   { return fFunctions; }
   TCollection      *GetListOfFunctionOverloads(const char* name) const;
   TSeqCollection   *GetListOfCleanups() const    { return fCleanups; }
   TSeqCollection   *GetListOfStreamerInfo() const { return fStreamerInfo; }
   TSeqCollection   *GetListOfMessageHandlers() const { return fMessageHandlers; }
   TCollection      *GetListOfClassGenerators() const { return fClassGenerators; }
   TCollection      *GetListOfEnums(Bool_t load = kFALSE);
   TCollection      *GetListOfFunctionTemplates();
   TDataType        *GetType(const char *name, Bool_t load = kFALSE) const;
   TFile            *GetFile() const { if (gDirectory != this) return gDirectory->GetFile(); else return 0;}
   TFile            *GetFile(const char *name) const;
   TFunctionTemplate*GetFunctionTemplate(const char *name);
   TObject          *GetFunction(const char *name) const;
   TGlobal          *GetGlobal(const char *name, Bool_t load = kFALSE) const;
   TGlobal          *GetGlobal(const TObject *obj, Bool_t load = kFALSE) const;
   TFunction        *GetGlobalFunction(const char *name, const char *params = 0, Bool_t load = kFALSE);
   TFunction        *GetGlobalFunctionWithPrototype(const char *name, const char *proto = 0, Bool_t load = kFALSE);
   Int_t             GetNclasses() const { return fClasses->GetSize(); }
   Int_t             GetNtypes() const { return fTypes->GetSize(); }
   TFolder          *GetRootFolder() const { return fRootFolder; }
   TProcessUUID     *GetUUIDs() const { return fUUIDs; }
   Int_t             IgnoreInclude(const char *fname, const char *expandedfname);
   Bool_t            IsBatch() const { return fBatch; }
   Bool_t            IsFolder() const { return kTRUE; }
   Bool_t            IsLineProcessing() const { return fLineIsProcessing ? kTRUE : kFALSE; }
   Bool_t            IsRootFile(const char *filename) const;
   void              ls(Option_t *option = "") const;
   Int_t             LoadClass(const char *classname, const char *libname, Bool_t check = kFALSE);
   TClass           *LoadClass(const char *name, Bool_t silent = kFALSE) const;
   Bool_t            MustClean() const { return fMustClean; }
   Bool_t            ReadingObject() const;
   void              RecursiveRemove(TObject *obj);
   static void       RegisterModule(const char* modulename,
                                    const char** headers,
                                    const char** includePaths,
                                    const char* payLoadCode,
                                    const char* fwdDeclCode,
                                    void (*triggerFunc)(),
                                    const FwdDeclArgsToKeepCollection_t& fwdDeclsArgToSkip,
                                    const char** classesHeaders,
                                    bool hasCxxModule = false);
   TObject          *Remove(TObject*);
   void              RemoveClass(TClass *);
   void              Reset(Option_t *option="");
   void              SaveContext();
   void              SetApplication(TApplication *app) { fApplication = app; }
   void              SetBatch(Bool_t batch = kTRUE) { fBatch = batch; }
   void              SetLineIsProcessing() { fLineIsProcessing++; }
   void              SetLineHasBeenProcessed() { if (fLineIsProcessing) fLineIsProcessing--; }
   void              SetReadingObject(Bool_t flag = kTRUE);
   void              SetMustClean(Bool_t flag = kTRUE) { fMustClean=flag; }
   void              Time(Int_t casetime=1) { fTimer = casetime; }
   Int_t             Timer() const { return fTimer; }

   //---- static functions
   static Int_t       DecreaseDirLevel();
   static Int_t       GetDirLevel();
   static Int_t       IncreaseDirLevel();
   static void        IndentLevel();
   static void        Initialize();
   static Bool_t      Initialized();
   static void        SetDirLevel(Int_t level = 0);
   static Int_t       ConvertVersionCode2Int(Int_t code);
   static Int_t       ConvertVersionInt2Code(Int_t v);
   static Int_t       RootVersionCode();
   static const std::vector<std::string> &AddExtraInterpreterArgs(const std::vector<std::string> &args);
   static const char**&GetExtraInterpreterArgs();

   static const TString& GetRootSys();
   static const TString& GetBinDir();
   static const TString& GetLibDir();
   static const TString& GetIncludeDir();
   static const TString& GetEtcDir();
   static const TString& GetSourceDir();

   // Backward compatibility function - do not use for new code
   static void ShutDown();

   ClassDef(TROOT,0)  //Top level (or root) structure for all classes
};


   TROOT *GetROOT();
   namespace Internal {
      R__EXTERN TROOT *gROOTLocal;

      inline void SetRequireCleanup(TObject &obj) {
         obj.SetBit(kIsReferenced);
         obj.SetUniqueID(0);
      }

      inline Bool_t RequiresCleanup(TObject &obj) {
         return obj.TestBit(kIsReferenced) && obj.GetUniqueID() == 0;
      }
   } // namespace CppyyLegacy

   /// \brief call RecursiveRemove for obj if gROOT is valid
   /// and obj.TestBit(kMustCleanup) is true.
   /// Note: this reset the kMustCleanup bit to allow
   /// harmless multiple call to this function.
   inline void CallRecursiveRemoveIfNeeded(TObject &obj)
   {
      if (obj.TestBit(kMustCleanup)) {
         TROOT *root = CppyyLegacy::Internal::gROOTLocal;
         if (root && root != &obj && (root->MustClean() || Internal::RequiresCleanup(obj))) {
            root->RecursiveRemove(&obj);
            obj.ResetBit(kMustCleanup);
         }
      }
   }

} // namespace CppyyLegacy

#define gROOT (::CppyyLegacy::GetROOT())

#endif

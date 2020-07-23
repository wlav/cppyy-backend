// @(#)root/meta:$Id$
// Author: Philippe Canal   23/2/02

/*************************************************************************
 * Copyright (C) 1995-2002, Rene Brun, Fons Rademakers and al.           *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TGenericClassInfo
#define ROOT_TGenericClassInfo

#include <string>
#include <vector>
#include "Rtypes.h"


namespace CppyyLegacy {

// Forward declarations
class TVirtualIsAProxy;

   namespace Internal {
      /// Returns a string with the demangled and normalized name for the given type.
      std::string GetDemangledTypeName(const std::type_info&);
   }

   namespace Detail {
      class TCollectionProxyInfo;
   }

   class TGenericClassInfo {
      // This class in not inlined because it is used is non time critical
      // section (the dictionaries) and inline would lead to too much
      // repetition of the code (once per class!).

      const Internal::TInitBehavior *fAction;
      TClass                     *fClass;
      const char                 *fClassName;
      const char                 *fDeclFileName;
      Int_t                       fDeclFileLine;
      DictFuncPtr_t               fDictionary;
      const std::type_info       &fInfo;
      const char                 *fImplFileName;
      Int_t                       fImplFileLine;
      TVirtualIsAProxy           *fIsA;
      Int_t                       fVersion;
      NewFunc_t                   fNew;
      NewArrFunc_t                fNewArray;
      DelFunc_t                   fDelete;
      DelArrFunc_t                fDeleteArray;
      DesFunc_t                   fDestructor;
      TClassStreamer             *fStreamer;
      ClassStreamerFunc_t         fStreamerFunc;
      ClassConvStreamerFunc_t     fConvStreamerFunc;
      TVirtualCollectionProxy    *fCollectionProxy;
      Int_t                       fSizeof;
      Int_t                       fPragmaBits;
      Detail::TCollectionProxyInfo *fCollectionProxyInfo;
      Detail::TCollectionProxyInfo *fCollectionStreamerInfo;

   public:
      TGenericClassInfo(const char *fullClassname,
                       const char *declFileName, Int_t declFileLine,
                       const std::type_info &info, const Internal::TInitBehavior *action,
                       DictFuncPtr_t dictionary,
                       TVirtualIsAProxy *isa, Int_t pragmabits, Int_t sizof);

      TGenericClassInfo(const char *fullClassname, Int_t version,
                       const char *declFileName, Int_t declFileLine,
                       const std::type_info &info, const Internal::TInitBehavior *action,
                       DictFuncPtr_t dictionary,
                       TVirtualIsAProxy *isa, Int_t pragmabits, Int_t sizof);

      TGenericClassInfo(const char *fullClassname, Int_t version,
                        const char *declFileName, Int_t declFileLine,
                        const Internal::TInitBehavior *action,
                        DictFuncPtr_t dictionary, Int_t pragmabits);

      void Init(Int_t pragmabits);
      ~TGenericClassInfo();

      const Internal::TInitBehavior    &GetAction() const;
      TClass                           *GetClass();
      const char                       *GetClassName() const;
      Detail::TCollectionProxyInfo     *GetCollectionProxyInfo() const;
      Detail::TCollectionProxyInfo     *GetCollectionStreamerInfo() const;
      const char                       *GetDeclFileName() const;
      Int_t                             GetDeclFileLine() const;
      DelFunc_t                         GetDelete() const;
      DelArrFunc_t                      GetDeleteArray() const;
      DesFunc_t                         GetDestructor() const;
      const char                       *GetImplFileName();
      Int_t                             GetImplFileLine();
      const std::type_info             &GetInfo() const;
      TVirtualIsAProxy                 *GetIsA() const;
      NewFunc_t                         GetNew() const;
      NewArrFunc_t                      GetNewArray() const;
      Int_t                             GetVersion() const;

      TClass                           *IsA(const void *obj);

      Short_t                           AdoptStreamer(TClassStreamer*);
      Short_t                           AdoptCollectionProxy(TVirtualCollectionProxy*);
      void                              AdoptCollectionProxyInfo(Detail::TCollectionProxyInfo*);
      void                              AdoptCollectionStreamerInfo(Detail::TCollectionProxyInfo*);
      Int_t                             SetDeclFile(const char *file, Int_t line);
      void                              SetDelete(DelFunc_t deleteFunc);
      void                              SetDeleteArray(DelArrFunc_t deleteArrayFunc);
      void                              SetDestructor(DesFunc_t destructorFunc);
      void                              SetFromTemplate();
      Int_t                             SetImplFile(const char *file, Int_t line);
      void                              SetNew(NewFunc_t newFunc);
      void                              SetNewArray(NewArrFunc_t newArrayFunc);
      Short_t                           SetStreamer(ClassStreamerFunc_t);
      void                              SetStreamerFunc(ClassStreamerFunc_t);
      void                              SetConvStreamerFunc(ClassConvStreamerFunc_t);
      Short_t                           SetVersion(Short_t version);

      //   protected:
   private:
      TGenericClassInfo(const TGenericClassInfo&); // Not implemented
      TGenericClassInfo& operator=(const TGenericClassInfo&); // Not implemented

   private:
      TGenericClassInfo();

   };

} // namespace CppyyLegacy

#endif

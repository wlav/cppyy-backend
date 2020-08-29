// @(#)root/meta:$Id$
// Author: Fons Rademakers   08/02/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TBaseClass
#define ROOT_TBaseClass

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TBaseClass                                                           //
//                                                                      //
// Description of a base class.                                         //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TDictionary.h"
#include "TClassRef.h"

#include <atomic>


namespace CppyyLegacy {

class BaseClassInfo_t;
class TClass;

class TBaseClass : public TDictionary {
#ifndef __CLING__
   using AtomicInt_t = std::atomic<ptrdiff_t>;
   static_assert(sizeof(std::atomic<ptrdiff_t>) == sizeof(ptrdiff_t),
                 "We require atomic<ptrdiff_t> and <ptrdiff_t> to have the same size but they are not");
#else
   // std::atomic is not yet supported in the I/O, so
   // we hide them from Cling
   using AtomicInt_t = ptrdiff_t;
#endif

private:
   TBaseClass(const TBaseClass &);          // Not implemented
   TBaseClass&operator=(const TBaseClass&); // Not implemented

private:
   BaseClassInfo_t    *fInfo;      //!pointer to CINT base class info
   TClassRef           fClassPtr;  // pointer to the base class TClass
   TClass             *fClass;     //!pointer to parent class
   AtomicInt_t         fDelta;     // BaseClassInfo_t offset (INT_MAX if unset)
   mutable AtomicInt_t fProperty;  // BaseClassInfo_t's properties
   Int_t               fSTLType;   // cache of IsSTLContainer()

public:
   TBaseClass(BaseClassInfo_t *info = 0, TClass *cl = 0);
   virtual     ~TBaseClass();
   const char    *GetTitle() const;
   TClass        *GetClassPointer(Bool_t load=kTRUE);
   ptrdiff_t      GetDelta();
   Bool_t         IsFolder() const {return kTRUE;}
   CppyyLegacy::ESTLType IsSTLContainer();
   Long_t         Property() const;
   void           SetClass(TClass* cl) { fClass = cl; }

   ClassDef(TBaseClass,2)  //Description of a base class
};

} // namespace CppyyLegacy
#endif

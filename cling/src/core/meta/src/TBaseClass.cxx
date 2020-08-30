// @(#)root/meta:$Id$
// Author: Fons Rademakers   08/02/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TBaseClass.h"
#include "TBuffer.h"
#include "TClass.h"
#include "TInterpreter.h"
#include <limits.h>

#include "TVirtualMutex.h" // For R__LOCKGUARD

/** \class TBaseClass
Each class (see TClass) has a linked list of its base class(es).
This class describes one single base class.
The base class info is obtained via the CINT api.
   see class TCling.

The base class information is used a.o. in to find all inherited methods.
*/


ClassImp(CppyyLegacy::TBaseClass);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Default TBaseClass ctor. TBaseClasses are constructed in TClass
/// via a call to TCling::CreateListOfBaseClasses().

TBaseClass::TBaseClass(BaseClassInfo_t *info, TClass *cl) :
   TDictionary(), fInfo(info), fClass(cl), fDelta(INT_MAX),
   fProperty(-1), fSTLType(-1)
{
   if (fInfo) SetName(gCling->BaseClassInfo_FullName(fInfo));
}

////////////////////////////////////////////////////////////////////////////////
/// TBaseClass dtor deletes adopted CINT BaseClassInfo object.

TBaseClass::~TBaseClass()
{
   gCling->BaseClassInfo_Delete(fInfo);
}

////////////////////////////////////////////////////////////////////////////////
/// Get pointer to the base class TClass.

TClass *TBaseClass::GetClassPointer(Bool_t load)
{
   if (!fClassPtr) {
      if (fInfo) fClassPtr = TClass::GetClass(gCling->BaseClassInfo_ClassInfo(fInfo),load);
      else fClassPtr = TClass::GetClass(fName, load);
   }
   return fClassPtr;
}

////////////////////////////////////////////////////////////////////////////////
/// Get offset from "this" to part of base class.

ptrdiff_t TBaseClass::GetDelta()
{
   // Initialized to INT_MAX to signal that it's unset; -1 is a valid value
   // meaning "cannot calculate base offset".
   if (fDelta == INT_MAX) {
      R__LOCKGUARD(gInterpreterMutex);
      if (Property() & kIsVirtualBase)
         fDelta = -1;
      else if (fInfo)
         fDelta = (ptrdiff_t)gCling->BaseClassInfo_Offset(fInfo);
   }
   return fDelta;
}

////////////////////////////////////////////////////////////////////////////////
/// Get base class description (comment).

const char *TBaseClass::GetTitle() const
{
   TClass *c = ((TBaseClass *)this)->GetClassPointer();
   return c ? c->GetTitle() : "";
}

////////////////////////////////////////////////////////////////////////////////
/// Return which type (if any) of STL container the data member is.

ESTLType TBaseClass::IsSTLContainer()
{
   // fSTLType is -1 if not yet evaluated.
   // fSTLType is -2 if no fInfo was available.

   if (fSTLType < 0) {
      if (!fInfo) {
         fSTLType = -2;
      } else {
         const char *type = gCling->BaseClassInfo_TmpltName(fInfo);
         if (!type)                                    fSTLType = kNotSTL;
         else if (!strcmp(type, "vector"))             fSTLType = kSTLvector;
         else if (!strcmp(type, "list"))               fSTLType = kSTLlist;
         else if (!strcmp(type, "forward_list"))       fSTLType = kSTLforwardlist;
         else if (!strcmp(type, "deque"))              fSTLType = kSTLdeque;
         else if (!strcmp(type, "map"))                fSTLType = kSTLmap;
         else if (!strcmp(type, "multimap"))           fSTLType = kSTLmultimap;
         else if (!strcmp(type, "set"))                fSTLType = kSTLset;
         else if (!strcmp(type, "multiset"))           fSTLType = kSTLmultiset;
         else if (!strcmp(type, "unordered_set"))      fSTLType = kSTLunorderedset;
         else if (!strcmp(type, "unordered_multiset")) fSTLType = kSTLunorderedmultiset;
         else if (!strcmp(type, "unordered_map"))      fSTLType = kSTLunorderedmap;
         else if (!strcmp(type, "unordered_multimap")) fSTLType = kSTLunorderedmultimap;
         /*else if (!strcmp(type, "bacic_stringstream") ||
                  !strcmp(type, "basic_ostringstream") ||
                  !strcmp(type, "basic_istringstream")) fSTLType = kSTLstream;
         else if (!strcmp(type, "basic_ostream") ||
                  !strcmp(type, "basic_istream") ||
                  !strcmp(type, "basic_ios"))          fSTLType = kSTLios;*/
         else                                          fSTLType = kNotSTL;
      }
   }
   if (fSTLType == -2) return kNotSTL;
   return (::CppyyLegacy::ESTLType) fSTLType;
}

////////////////////////////////////////////////////////////////////////////////
/// Get property description word. For meaning of bits see EProperty.

Long_t TBaseClass::Property() const
{
   if (fProperty == -1 && fInfo) {
      R__LOCKGUARD(gInterpreterMutex);
      fProperty = gCling->BaseClassInfo_Property(fInfo);
   }
   return fProperty;
}

////////////////////////////////////////////////////////////////////////////////
/// Stream an object of TBaseClass. Triggers the calculation of the
/// cache variables to store them.

void TBaseClass::Streamer(TBuffer& b) {
   if (b.IsReading()) {
      b.ReadClassBuffer(Class(), this);
   } else {
      // Writing.
      // Calculate cache properties first.
      GetDelta();
      Property();
      IsSTLContainer();
      b.WriteClassBuffer(Class(), this);
   }
}

} // namespace CppyyLegacy

// @(#)root/meta:$Id$
// Author: Rene Brun   09/02/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TMethod
 Each ROOT class (see TClass) has a linked list of methods.
 This class describes one single method (member function).
 The method info is obtained via the CINT api. See class TCling.
*/

#include "TClass.h"
#include "TMethod.h"
#include "TMethodArg.h"
#include "TROOT.h"
#include "TApplication.h"
#include "TInterpreter.h"
#include "Strlen.h"
#include "TDataMember.h"


ClassImp(CppyyLegacy::TMethod);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Default TMethod ctor. TMethods are constructed in TClass.
/// Comment strings are pre-parsed to find out whether the method is
/// a context-menu item.

TMethod::TMethod(MethodInfo_t *info, TClass *cl) : TFunction(info)
{
   fClass        = cl;
}

////////////////////////////////////////////////////////////////////////////////
/// Copy ctor.

TMethod::TMethod(const TMethod& orig) : TFunction(orig)
{
   fClass        = orig.fClass;
   fGetter       = orig.fGetter;
}

////////////////////////////////////////////////////////////////////////////////
/// Assignment operator.

TMethod& TMethod::operator=(const TMethod& rhs)
{
   if (this != &rhs) {
      TFunction::operator=(rhs);
      fClass        = rhs.fClass;
      fGetter       = rhs.fGetter;
   }
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
/// Cleanup.

TMethod::~TMethod()
{
}

////////////////////////////////////////////////////////////////////////////////
/// Clone method.

TObject *TMethod::Clone(const char *newname) const
{
   TNamed *newobj = new TMethod(*this);
   if (newname && strlen(newname)) newobj->SetName(newname);
   return newobj;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns a comment string from the class declaration.

const char *TMethod::GetCommentString()
{
   return fInfo ? gCling->MethodInfo_Title(fInfo) : "";
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if this function object is pointing to a currently
/// loaded function.  If a function is unloaded after the TMethod
/// is created, the TMethod will be set to be invalid.

Bool_t TMethod::IsValid()
{
   // Register the transaction when checking the validity of the object.
   if (!fInfo && UpdateInterpreterStateMarker()) {
      DeclId_t newId = gInterpreter->GetFunction(fClass->GetClassInfo(), fName);
      if (newId) {
         MethodInfo_t *info = gInterpreter->MethodInfo_Factory(newId);
         Update(info);
      }
      return newId != 0;
   }
   return fInfo != 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns methodarg list
TList *TMethod::GetListOfMethodArgs()
{
   if (!fMethodArgs)
      TFunction::GetListOfMethodArgs();
   return fMethodArgs;
}

////////////////////////////////////////////////////////////////////////////////
/// Update the TMethod to reflect the new info.
///
/// This can be used to implement unloading (info == 0) and then reloading
/// (info being the 'new' decl address).

Bool_t TMethod::Update(MethodInfo_t *info)
{
   if (TFunction::Update(info))
      return kTRUE;
   return kFALSE;
}

} // namespace CppyyLegacy

// @(#)root/meta:$Id$
// Author: Rene Brun   09/02/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TMethod
#define ROOT_TMethod

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TMethod                                                              //
//                                                                      //
// Dictionary of a member function (method).                            //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TFunction.h"


namespace CppyyLegacy {

class TList;
class TDataMember;
class TClass;

class TMethod : public TFunction {

private:
   TClass                 *fClass;           //pointer to the class
   TString                 fGetter;          //state getter in case this is a *TOGGLE method

public:
                           TMethod(MethodInfo_t *info = 0, TClass *cl = 0);
                           TMethod(const TMethod &org);
   TMethod&                operator=(const TMethod &rhs);
   virtual                ~TMethod();
   virtual TObject        *Clone(const char *newname="") const;
   TClass                 *GetClass() const { return fClass; }
   virtual Bool_t          IsValid();
   virtual const char     *GetCommentString();
   virtual const char     *Getter() const { return fGetter; }
   virtual TList          *GetListOfMethodArgs();

   virtual Bool_t          Update(MethodInfo_t *info);

   ClassDef(TMethod,0)  //Dictionary for a class member function (method)
};

} // namespace CppyyLegacy
#endif

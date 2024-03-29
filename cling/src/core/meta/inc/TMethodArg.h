// @(#)root/meta:$Id$
// Author: Rene Brun   04/02/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TMethodArg
#define ROOT_TMethodArg

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TMethodArg                                                           //
//                                                                      //
// Dictionary interface for a method argument.                          //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TDictionary.h"
#include "TDataMember.h"


namespace CppyyLegacy {

class TFunction;
class TMethod;

class TMethodArg : public TDictionary {

friend class TMethod;

private:
   TMethodArg(const TMethodArg&);    // Not implemented
   TMethodArg& operator=(const TMethodArg&);    // Not implemented

   MethodArgInfo_t   *fInfo;         //pointer to CINT method argument info
   TFunction         *fMethod;       //pointer to the method or global function

public:
   TMethodArg(MethodArgInfo_t *info = 0, TFunction *method = 0);
   virtual       ~TMethodArg();
   const char    *GetDefault() const;
   TFunction     *GetMethod() const { return fMethod; }
   const char    *GetFullTypeName() const;
   std::string    GetTypeNormalizedName() const;
   Long_t         Property() const;
   TypeInfo_t    *GetTypeInfo() const;

   void           Update(MethodArgInfo_t *info);

   ClassDef(TMethodArg,0)  //Dictionary for a method argument
};

} // namespace CppyyLegacy
#endif

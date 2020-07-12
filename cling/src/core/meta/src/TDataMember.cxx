// @(#)root/meta:$Id$
// Author: Fons Rademakers   04/02/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TDataMember
*/

#include "TDataMember.h"

#include "Strlen.h"
#include "TClass.h"
#include "TClassEdit.h"
#include "TDataType.h"
#include "TEnum.h"
#include "TEnumConstant.h"
#include "TGlobal.h"
#include "TInterpreter.h"
#include "TIterator.h"
#include "TList.h"
#include "TListOfDataMembers.h"
#include "TMethod.h"
#include "TRealData.h"
#include "TROOT.h"
#include "TVirtualMutex.h"

#include <cassert>
#include <cctype>
#include <stdlib.h>


ClassImp(CppyyLegacy::TDataMember);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Default TDataMember ctor. TDataMembers are constructed in TClass
/// via a call to TCling::CreateListOfDataMembers(). It parses the comment
/// string, initializes optionlist and getter/setter methods.

TDataMember::TDataMember(DataMemberInfo_t *info, TClass *cl) : TDictionary()
{
   fInfo        = info;
   fClass       = cl;
   fDataType    = 0;
   fOptions     = 0;
   fOffset      = -1;
   fProperty    = -1;
   fSTLCont     = -1;
   fArrayDim    = -1;
   fArrayMaxIndex=0;
   if (!fInfo && !fClass) return; // default ctor is called

   Init(false);
}

////////////////////////////////////////////////////////////////////////////////
/// Routines called by the constructor and Update to reset the member's
/// information.
/// afterReading is set when initializing after reading through Streamer().

void TDataMember::Init(bool afterReading)
{
   const char *t = 0;
   if (!afterReading) {
      // Initialize from fInfo
      if (!fInfo || !gInterpreter->DataMemberInfo_IsValid(fInfo)) return;

      fFullTypeName = gCling->DataMemberInfo_TypeName(fInfo);
      fTrueTypeName = gCling->DataMemberInfo_TypeTrueName(fInfo);
      fTypeName     = gCling->TypeName(fTrueTypeName);
      SetName(gCling->DataMemberInfo_Name(fInfo));
      t = gCling->DataMemberInfo_Title(fInfo);
      SetTitle(t);
   } else {
      // We have read the persistent data members.
      t = GetTitle();
   }
   if (t && t[0] != '!') SetBit(kObjIsPersistent);
   fDataType = 0;
   if (IsBasic() || IsEnum()) {
      if (IsBasic()) {
         const char *name = GetFullTypeName();
         if (strcmp(name, "unsigned char") != 0 &&
             strncmp(name, "unsigned short", sizeof ("unsigned short")) != 0 &&
             strcmp(name, "unsigned int") != 0 &&
             strncmp(name, "unsigned long", sizeof ("unsigned long")) != 0)
            // strncmp() also covers "unsigned long long"
            name = GetTypeName();
         fDataType = gROOT->GetType(name);

         if (fDataType==0) {
            // humm we did not find it ... maybe it's a typedef that has not been loaded yet.
            // (this can happen if the executable does not have a TApplication object).
            fDataType = gROOT->GetType(name,kTRUE);
         }
      } else {
         fDataType = gROOT->GetType("Int_t", kTRUE); // In rare instance we are called before Int_t has been added to the list of types in TROOT, the kTRUE insures it is there.
      }
      //         if (!fDataType)
      //            Error("TDataMember", "basic data type %s not found in list of basic types",
      //                  GetTypeName());
   }

   if (afterReading)
      return;

   // If option string exist in comment - we'll parse it and create
   // list of options

   // Option-list string has a form:
   // *OPTION={GetMethod="GetXXX";SetMethod="SetXXX";
   //          Items=(0="NULL ITEM","one"="First Item",kRed="Red Item")}
   //
   // As one can see it is possible to specify value as either numerical
   // value , string  or enum.
   // One can also specify implicitly names of Getter/Setter methods.

   char cmt[2048];
   char opt[2048];
   char *opt_ptr = 0;
   const char *ptr1    = 0;
   char *ptr2    = 0;
   char *ptr3    = 0;
   char *tok     = 0;
   Int_t cnt     = 0;
   Int_t token_cnt;
   Int_t i;

   strlcpy(cmt,GetTitle(),2048);

   if ((opt_ptr=strstr(cmt,"*OPTION={"))) {

      // If we found it - parsing...

      //let's cut the part lying between {}
      char *rest;
      ptr1 = R__STRTOK_R(opt_ptr, "{}", &rest); // starts tokenizing:extracts "*OPTION={"
      if (ptr1 == 0) {
         Fatal("TDataMember","Internal error, found \"*OPTION={\" but not \"{}\" in %s.",GetTitle());
         return;
      }
      ptr1 = R__STRTOK_R(nullptr, "{}", &rest); // And now we have what we need in ptr1!!!
      if (ptr1 == 0) {
         Fatal("TDataMember","Internal error, found \"*OPTION={\" but not \"{}\" in %s.",GetTitle());
         return;
      }

      //and save it:
      strlcpy(opt,ptr1,2048);

      // Let's extract sub-tokens extracted by ';' sign.
      // We'll put'em in an array for convenience;
      // You have to do it in this manner because you cannot use nested tokenizing

      char *tokens[256];           // a storage for these sub-tokens.
      token_cnt = 0;
      cnt       = 0;

      do {                          //tokenizing loop
         ptr1 = R__STRTOK_R((char *)(cnt++ ? nullptr : opt), ";", &rest);
         if (ptr1){
            Int_t nch = strlen(ptr1)+1;
            tok=new char[nch];
            strlcpy(tok,ptr1,nch);
            tokens[token_cnt]=tok;
            token_cnt++;
         }
      } while (ptr1);

      // OK! Now let's check whether we have Get/Set methods encode in any string
      for (i=0;i<token_cnt;i++) {
         if (strstr(tokens[i],"GetMethod")) {
            ptr1 = R__STRTOK_R(tokens[i], "\"", &rest); // tokenizing-strip text "GetMethod"
            if (ptr1 == 0) {
               Fatal("TDataMember","Internal error, found \"GetMethod\" but not \"\\\"\" in %s.",GetTitle());
               return;
            }
            ptr1 = R__STRTOK_R(nullptr, "\"", &rest); // tokenizing - name is in ptr1!
            if (ptr1 == 0) {
               Fatal("TDataMember","Internal error, found \"GetMethod\" but not \"\\\"\" in %s.",GetTitle());
               return;
            }

            continue; //next item!
         }

         if (strstr(tokens[i],"SetMethod")) {
            ptr1 = R__STRTOK_R(tokens[i], "\"", &rest);
            if (ptr1 == 0) {
               Fatal("TDataMember","Internal error, found \"SetMethod\" but not \"\\\"\" in %s.",GetTitle());
               return;
            }
            ptr1 = R__STRTOK_R(nullptr, "\"", &rest); // name of Setter in ptr1
            if (ptr1 == 0) {
               Fatal("TDataMember","Internal error, found \"SetMethod\" but not \"\\\"\" in %s.",GetTitle());
               return;
            }
         }
      }

      //Now let's parse option strings...

      Int_t  opt_cnt    = 0;
      std::unique_ptr<TList> optionlist{new TList()};       //storage for options strings

      for (i=0;i<token_cnt;i++) {
         if (strstr(tokens[i],"Items")) {
            ptr1 = R__STRTOK_R(tokens[i], "()", &rest);
            if (ptr1 == 0) {
               Fatal("TDataMember","Internal error, found \"Items\" but not \"()\" in %s.",GetTitle());
               return;
            }
            ptr1 = R__STRTOK_R(nullptr, "()", &rest);
            if (ptr1 == 0) {
               Fatal("TDataMember","Internal error, found \"Items\" but not \"()\" in %s.",GetTitle());
               return;
            }

            char opts[2048];  //and save it!
            strlcpy(opts,ptr1,2048);

            //now parse it...
            //firstly we just store strings like: xxx="Label Name"
            //We'll store it in TOptionListItem objects, because they're derived
            //from TObject and thus can be stored in TList.
            //It's not elegant but works.
            do {
               ptr1 = R__STRTOK_R(opt_cnt++ ? nullptr : opts, ",", &rest); // options extraction
               if (ptr1) {
                  TOptionListItem *it = new TOptionListItem(this,1,0,0,ptr1,"");
                  optionlist->Add(it);
               }
            } while(ptr1);

         }
      }

      //having all options extracted and put into list, we finally can parse
      //them to create a list of options...

      fOptions = new TList();                //create the list

      TIter next(optionlist.get());                //we'll iterate through all
                                             //strings containing options
      TOptionListItem *it  = 0;
      TOptionListItem *it1 = 0;
      while ((it=(TOptionListItem*)next())) {

         ptr1 = it->fOptName;  // We will change the value of OptName ... but it is fine since we delete the object at the end of the loop.
         Bool_t islabel = (ptr1[0]=='\"');   // value is label or numerical?
         ptr2 = R__STRTOK_R((char *)ptr1, "=\"", &rest); // extract LeftHandeSide
         ptr3 = R__STRTOK_R(nullptr, "=\"", &rest);            // extract RightHandedSize

         if (islabel) {
            it1=new TOptionListItem(this,-9999,0,0,ptr3,ptr2);
            fOptions->Add(it1);
         }  else {
            char *strtolResult;
            Long_t l = std::strtol(ptr1, &strtolResult, 10);
            bool isnumber = (strtolResult != ptr1);

            if (!isnumber) {
               TGlobal *enumval = gROOT->GetGlobal(ptr1, kTRUE);
               if (enumval) {
                  Int_t *value = (Int_t *)(enumval->GetAddress());
                  // We'll try to find global enum existing in ROOT...
                  l = (Long_t)(*value);
               } else if (IsEnum()) {
                  TObject *obj = fClass->GetListOfDataMembers(false)->FindObject(ptr1);
                  if (obj)
                     l = ((TEnumConstant *)obj)->GetValue();
                  else
                     l = (Long_t)gInterpreter->Calc(Form("%s;", ptr1));
               } else {
                  Fatal("TDataMember", "Internal error, couldn't recognize enum/global value %s.", ptr1);
               }
            }

            it1 = new TOptionListItem(this,l,0,0,ptr3,ptr1);
            fOptions->Add(it1);
         }

         optionlist->Remove(it);         //delete this option string from list
         delete it;                      // and dispose of it.

      }

      // Garbage collection

      //And dispose tokens string...
      for (i=0;i<token_cnt;i++) if(tokens[i]) delete [] tokens[i];

   // if option string does not exist but it's an Enum - parse it!!!!
   } else if (IsEnum()) {
      fOptions = new TList();
      if (TEnum* enumDict = TEnum::GetEnum(GetTypeName()) ){
         TIter iEnumConst(enumDict->GetConstants());
         while (TEnumConstant* enumConst = (TEnumConstant*)iEnumConst()) {
            TOptionListItem *it
               = new TOptionListItem(this, enumConst->GetValue(),0,0,
                                     enumConst->GetName(),enumConst->GetName());
            fOptions->Add(it);
         }
      }

   // and the case od Bool_t : we add items "ON" and "Off"
   } else if (!strncmp(GetFullTypeName(),"Bool_t",6)){

      fOptions = new TList();
      TOptionListItem *it = new TOptionListItem(this,1,0,0,"ON",0);
      fOptions->Add(it);
      it = new TOptionListItem(this,0,0,0,"Off",0);
      fOptions->Add(it);

   } else fOptions = 0;

}

////////////////////////////////////////////////////////////////////////////////
/// copy constructor

TDataMember::TDataMember(const TDataMember& dm) :
  TDictionary(dm),
  fInfo(gCling->DataMemberInfo_FactoryCopy(dm.fInfo)),
  fClass(dm.fClass),
  fDataType(dm.fDataType),
  fOffset(dm.fOffset),
  fSTLCont(dm.fSTLCont),
  fProperty(dm.fProperty),
  fArrayDim(dm.fArrayDim),
  fArrayMaxIndex( dm.fArrayDim ? new Int_t[dm.fArrayDim] : 0),
  fArrayIndex(dm.fArrayIndex),
  fTypeName(dm.fTypeName),
  fFullTypeName(dm.fFullTypeName),
  fTrueTypeName(dm.fTrueTypeName),
  fOptions(dm.fOptions ? (TList*)dm.fOptions->Clone() : 0)
{
   for(Int_t d = 0; d < fArrayDim; ++d)
      fArrayMaxIndex[d] = dm.fArrayMaxIndex[d];
}

////////////////////////////////////////////////////////////////////////////////
/// assignment operator

TDataMember& TDataMember::operator=(const TDataMember& dm)
{
   if(this!=&dm) {
      gCling->DataMemberInfo_Delete(fInfo);
      if (fOptions) {
         fOptions->Delete();
         delete fOptions;
         fOptions = 0;
      }

      TDictionary::operator=(dm);
      fInfo= gCling->DataMemberInfo_FactoryCopy(dm.fInfo);
      fClass=dm.fClass;
      fDataType=dm.fDataType;
      fOffset=dm.fOffset;
      fSTLCont=dm.fSTLCont;
      fProperty=dm.fProperty;
      fArrayDim = dm.fArrayDim;
      fArrayMaxIndex = dm.fArrayDim ? new Int_t[dm.fArrayDim] : 0;
      for(Int_t d = 0; d < fArrayDim; ++d)
         fArrayMaxIndex[d] = dm.fArrayMaxIndex[d];
      fArrayIndex = dm.fArrayIndex;
      fTypeName=dm.fTypeName;
      fFullTypeName=dm.fFullTypeName;
      fTrueTypeName=dm.fTrueTypeName;
      fOptions = dm.fOptions ? (TList*)dm.fOptions->Clone() : 0;
   }
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
/// TDataMember dtor deletes adopted CINT DataMemberInfo object.

TDataMember::~TDataMember()
{
   delete [] fArrayMaxIndex;
   gCling->DataMemberInfo_Delete(fInfo);
   if (fOptions) {
      fOptions->Delete();
      delete fOptions;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return number of array dimensions.

Int_t TDataMember::GetArrayDim() const
{
   if (fArrayDim<0 && fInfo) {
      R__LOCKGUARD(gInterpreterMutex);
      TDataMember *dm = const_cast<TDataMember*>(this);
      dm->fArrayDim = gCling->DataMemberInfo_ArrayDim(fInfo);
      // fArrayMaxIndex should be zero
      if (dm->fArrayDim) {
         dm->fArrayMaxIndex = new Int_t[fArrayDim];
         for(Int_t dim = 0; dim < dm->fArrayDim; ++dim) {
            dm->fArrayMaxIndex[dim] = gCling->DataMemberInfo_MaxIndex(fInfo,dim);
         }
      }
   }
   return fArrayDim;
}

////////////////////////////////////////////////////////////////////////////////
/// If the data member is pointer and has a valid array size in its comments
/// GetArrayIndex returns a string pointing to it;
/// otherwise it returns an empty string.

const char *TDataMember::GetArrayIndex() const
{
   if (!IsaPointer()) return "";
   if (fArrayIndex.Length()==0 && fInfo) {
      R__LOCKGUARD(gInterpreterMutex);
      TDataMember *dm = const_cast<TDataMember*>(this);
      const char* val = gCling->DataMemberInfo_ValidArrayIndex(fInfo);
      if (val) dm->fArrayIndex = val;
      else dm->fArrayIndex.Append((Char_t)0); // Make length non-zero but string still empty.
   }
   return fArrayIndex;
}

////////////////////////////////////////////////////////////////////////////////

TDictionary::DeclId_t TDataMember::GetDeclId() const
{
   if (fInfo) return gInterpreter->GetDeclId(fInfo);
   else return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Return maximum index for array dimension "dim".

Int_t TDataMember::GetMaxIndex(Int_t dim) const
{
   if (fArrayDim<0 && fInfo) {
      return gCling->DataMemberInfo_MaxIndex(fInfo,dim);
   } else {
      if (dim < 0 || dim >= fArrayDim) return -1;
      return fArrayMaxIndex[dim];
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Get type of data member, e,g.: "class TDirectory*" -> "TDirectory".

const char *TDataMember::GetTypeName() const
{
   if (fProperty==(-1)) Property();
   return fTypeName.Data();
}

////////////////////////////////////////////////////////////////////////////////
/// Get full type description of data member, e,g.: "class TDirectory*".

const char *TDataMember::GetFullTypeName() const
{
   if (fProperty==(-1)) Property();

   return fFullTypeName.Data();
}

////////////////////////////////////////////////////////////////////////////////
/// Get full type description of data member, e,g.: "class TDirectory*".

const char *TDataMember::GetTrueTypeName() const
{
   return fTrueTypeName.Data();
}

////////////////////////////////////////////////////////////////////////////////
/// Get offset from "this".

intptr_t TDataMember::GetOffset() const
{
   if (fOffset != (intptr_t)-1) return fOffset;

   R__LOCKGUARD(gInterpreterMutex);
   //case of an interpreted or emulated class
   if (fClass->GetDeclFileLine() < 0) {
      ((TDataMember*)this)->fOffset = gCling->DataMemberInfo_Offset(fInfo);
      return fOffset;
   }
   //case of a compiled class
   //Note that the offset cannot be computed in case of an abstract class
   //for which the list of real data has not yet been computed via
   //a real daughter class.
   TString dmbracket;
   dmbracket.Form("%s[",GetName());
   fClass->BuildRealData();
   TIter next(fClass->GetListOfRealData());
   TRealData *rdm;
   intptr_t offset = -1;
   while ((rdm = (TRealData*)next())) {
      char *rdmc = (char*)rdm->GetName();
      //next statement required in case a class and one of its parent class
      //have data members with the same name
      if (this->IsaPointer() && rdmc[0] == '*') rdmc++;

      if (rdm->GetDataMember() != this) continue;
      if (strcmp(rdmc,GetName()) == 0) {
         offset = rdm->GetThisOffset();
         break;
      }
      if (strcmp(rdm->GetName(),GetName()) == 0) {
         if (rdm->IsObject()) {
            offset = rdm->GetThisOffset();
            break;
         }
      }
      if (strstr(rdm->GetName(),dmbracket.Data())) {
         offset = rdm->GetThisOffset();
         break;
      }
   }
   ((TDataMember*)this)->fOffset = offset;
   return fOffset;
}

////////////////////////////////////////////////////////////////////////////////
/// Get offset from "this" using the information in CINT only.

intptr_t TDataMember::GetOffsetCint() const
{
   if (fOffset != (intptr_t)-1) return fOffset;

   R__LOCKGUARD(gInterpreterMutex);
   TDataMember *dm = const_cast<TDataMember*>(this);

   if (dm->IsValid()) return gCling->DataMemberInfo_Offset(dm->fInfo);
   else return -1;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the sizeof the underlying type of the data member
/// (i.e. if the member is an array sizeof(member)/length)

Int_t TDataMember::GetUnitSize() const
{
   if (IsaPointer()) return sizeof(void*);
   if (IsEnum()    ) return sizeof(Int_t);
   if (IsBasic()   ) return GetDataType()->Size();

   TClass *cl = TClass::GetClass(GetTypeName());
   if (!cl) cl = TClass::GetClass(GetTrueTypeName());
   if ( cl) return cl->Size();

   Warning("GetUnitSize","Can not determine sizeof(%s)",GetTypeName());
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if data member is a basic type, e.g. char, int, long...

Bool_t TDataMember::IsBasic() const
{
   if (fProperty == -1) Property();
   return (fProperty & kIsFundamental) ? kTRUE : kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if data member is an enum.

Bool_t TDataMember::IsEnum() const
{
   if (fProperty == -1) Property();
   return (fProperty & kIsEnum) ? kTRUE : kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if data member is a pointer.

Bool_t TDataMember::IsaPointer() const
{
   if (fProperty == -1) Property();
   return (fProperty & kIsPointer) ? kTRUE : kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// The return type is defined in TDictionary (kVector, kList, etc.)

int TDataMember::IsSTLContainer()
{
   if (fSTLCont != -1) return fSTLCont;
   R__LOCKGUARD(gInterpreterMutex);
   fSTLCont = TClassEdit::UnderlyingIsSTLCont(GetTrueTypeName());
   return fSTLCont;
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if this data member object is pointing to a currently
/// loaded data member.  If a function is unloaded after the TDataMember
/// is created, the TDataMember will be set to be invalid.

Bool_t TDataMember::IsValid()
{
   if (fOffset != (intptr_t)-1) return kTRUE;

   // Register the transaction when checking the validity of the object.
   if (!fInfo && UpdateInterpreterStateMarker()) {
      DeclId_t newId = gInterpreter->GetDataMember(fClass->GetClassInfo(), fName);
      if (newId) {
         DataMemberInfo_t *info
            = gInterpreter->DataMemberInfo_Factory(newId, fClass->GetClassInfo());
         Update(info);
         // We need to make sure that the list of data member is properly
         // informed and updated.
         TListOfDataMembers *lst = dynamic_cast<TListOfDataMembers*>(fClass->GetListOfDataMembers());
         lst->Update(this);
      }
      return newId != 0;
   }
   return fInfo != 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Get property description word. For meaning of bits see EProperty.

Long_t TDataMember::Property() const
{
   if (fProperty!=(-1)) return fProperty;

   R__LOCKGUARD(gInterpreterMutex);
   TDataMember *t = (TDataMember*)this;

   if (!fInfo || !gCling->DataMemberInfo_IsValid(fInfo)) return 0;
   int prop  = gCling->DataMemberInfo_Property(fInfo);
   int propt = gCling->DataMemberInfo_TypeProperty(fInfo);
   t->fProperty = prop|propt;

   t->fFullTypeName = gCling->DataMemberInfo_TypeName(fInfo);
   t->fTrueTypeName = gCling->DataMemberInfo_TypeTrueName(fInfo);
   t->fTypeName     = gCling->TypeName(fTrueTypeName);

   t->fName  = gCling->DataMemberInfo_Name(fInfo);
   t->fTitle = gCling->DataMemberInfo_Title(fInfo);

   return fProperty;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns list of options - list of TOptionListItems

TList *TDataMember::GetOptions() const
{
   return fOptions;
}

////////////////////////////////////////////////////////////////////////////////
/// Update the TFunction to reflect the new info.
///
/// This can be used to implement unloading (info == 0) and then reloading
/// (info being the 'new' decl address).

Bool_t TDataMember::Update(DataMemberInfo_t *info)
{
   R__LOCKGUARD(gInterpreterMutex);

   if (fInfo) gCling->DataMemberInfo_Delete(fInfo);
   if (fOptions) {
      fOptions->Delete();
      SafeDelete(fOptions);
   }

   if (info == 0) {
      fOffset      = -1;
      fProperty    = -1;
      fSTLCont     = -1;
      fArrayDim    = -1;
      delete [] fArrayMaxIndex;
      fArrayMaxIndex=0;
      fArrayIndex.Clear();

      fInfo = 0;
      return kTRUE;
   } else {
      fInfo = info;
      Init(false);
      return kTRUE;
   }
}


////////////////////////////////////////////////////////////////////////////////
/// Stream an object of TDataMember. Forces calculation of all cached
/// (and persistent) values.

void TDataMember::Streamer(TBuffer& b) {
   if (b.IsReading()) {
      b.ReadClassBuffer(Class(), this);
      Init(true /*reading*/);
   } else {
      // Writing.
      if (fProperty & kIsStatic) {
         // We have a static member and in this case fOffset contains the
         // actual address in memory of the data, it will be different everytime,
         // let's not record it.
         fOffset = -1;
      } else {
         GetOffset();
      }
      IsSTLContainer();
      GetArrayDim();
      GetArrayIndex();
      Property(); // also calculates fTypeName and friends
      b.WriteClassBuffer(Class(), this);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor.

TOptionListItem::TOptionListItem(TDataMember *d, Long_t val, Long_t valmask,
                 Long_t tglmask,const char *name, const char *label)
{
   fDataMember    = d;
   fValue         = val;
   fValueMaskBit  = valmask;
   fToggleMaskBit = tglmask;
   if (name) {
      fOptName = name;
   }

   if (label) {
      fOptLabel = label;
   }
}

} // namespace CppyyLegacy

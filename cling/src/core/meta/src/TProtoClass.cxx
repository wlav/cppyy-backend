// @(#)root/meta:$
// Author: Axel Naumann 2014-05-02

/*************************************************************************
 * Copyright (C) 1995-2014, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TProtoClass
Persistent version of a TClass.
*/

#include "TProtoClass.h"

#include "TBaseClass.h"
#include "TClass.h"
#include "TClassEdit.h"
#include "TDataMember.h"
#include "TEnum.h"
#include "TInterpreter.h"
#include "TList.h"
#include "TListOfDataMembers.h"
#include "TListOfEnums.h"
#include "TRealData.h"
#include "TError.h"

#include <cassert>


namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Initialize a TProtoClass from a TClass.

TProtoClass::TProtoClass(TClass* cl):
   TNamed(*cl), fBase(cl->GetListOfBases()),
   fEnums(cl->GetListOfEnums()), fSizeof(cl->Size()), fCheckSum(cl->fCheckSum),
   fCanSplit(cl->fCanSplit), fStreamerType(cl->fStreamerType), fProperty(cl->fProperty),
   fClassProperty(cl->fClassProperty)
{
   if (cl->Property() & kIsNamespace){
      //fData=new TListOfDataMembers();
      fEnums=nullptr;
      //fPRealData=nullptr;
      fOffsetStreamer=0;
      return;
   }
   TListOfEnums *enums = dynamic_cast<TListOfEnums*>(fEnums);
   if (enums && !enums->fIsLoaded) {
      // Make sure all the enum information is loaded
      enums->Load();
   }
   // initialize list of data members (fData)
   TList * dataMembers = cl->GetListOfDataMembers();
   if (dataMembers && dataMembers->GetSize() > 0) {
      fData.reserve(dataMembers->GetSize() );
      for (auto * obj : *dataMembers) {
         TDataMember * dm = dynamic_cast<TDataMember*>(obj);
         fData.push_back(dm);
      }
   }

   fPRealData.reserve(100);

   if (!cl->GetCollectionProxy()) {
      // Build the list of RealData before we access it:
      cl->BuildRealData(0, true /*isTransient*/);
      // The data members are ordered as follows:
      // - this class's data members,
      // - foreach base: base class's data members.
      // fPRealData encodes all TProtoRealData objects with a
      // TObjString to signal a new class.
      TClass* clCurrent = cl;
      fDepClasses.push_back(cl->GetName() );
      TRealData* precRd = nullptr;
      for (auto realDataObj: *cl->GetListOfRealData()) {
         TRealData *rd = (TRealData*)realDataObj;
         if (!precRd) precRd = rd;
         if (!rd->GetDataMember()) continue;
         TClass* clRD = rd->GetDataMember()->GetClass();
         TProtoRealData protoRealData(rd);
         if (clRD != clCurrent) {
            // here I have a new class
            fDepClasses.push_back(clRD->GetName() );
            clCurrent = clRD;
            protoRealData.fClassIndex = fDepClasses.size()-1;
            if (rd->TestBit(TRealData::kTransient))
               protoRealData.SetFlag(TProtoRealData::kIsTransient,true);
            else
               protoRealData.SetFlag(TProtoRealData::kIsTransient,false);

            precRd = rd;
         }
         fPRealData.push_back(protoRealData);
      }

   }

   // this crashes
   cl->CalculateStreamerOffset();
   fOffsetStreamer = cl->fOffsetStreamer;
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor.

TProtoClass::~TProtoClass()
{
   Delete();
}

////////////////////////////////////////////////////////////////////////////////
/// Delete the containers that are usually owned by their TClass.
/// if (fPRealData) fPRealData->Delete(opt);
/// delete fPRealData; fPRealData = 0;

void TProtoClass::Delete(Option_t* opt /*= ""*/) {
   if (fBase) fBase->Delete(opt);
   delete fBase; fBase = 0;

   for (auto dm: fData) {
      delete dm;
   }

   if (fEnums) fEnums->Delete(opt);
   delete fEnums; fEnums = 0;

   if (gErrorIgnoreLevel==-2) printf("Delete the protoClass %s \n",GetName());
}

////////////////////////////////////////////////////////////////////////////////
/// Move data from this TProtoClass into cl.
/// Returns 'false' if nothing was done.  This can happen in the case where
/// there is more than one dictionary for the same entity.  Note having
/// duplicate dictionary is acceptable for namespace or STL collections.

Bool_t TProtoClass::FillTClass(TClass* cl) {
   if (cl->fRealData || cl->fBase.load() || cl->fData || cl->fEnums.load() || cl->fSizeof != -1 || cl->fCanSplit >= 0 ||
       cl->fProperty != (-1)) {

      if (cl->GetCollectionType() != CppyyLegacy::kNotSTL) {
         // We are in the case of collection, duplicate dictionary are allowed
         // (and even somewhat excepted since they can be auto asked for).
         // They do not always have a TProtoClass for them.  In particular
         // the one that are pre-generated in the ROOT build (in what was once
         // called the cintdlls) do not have a pcms, neither does vector<string>
         // which is part of libCoreLegacy proper.
         if (gDebug > 0)
            Info("FillTClass", "Returning w/o doing anything. %s is a STL collection.",cl->GetName());
         return kFALSE;
      }
      if (cl->Property() & kIsNamespace) {
         if (gDebug > 0)
            Info("FillTClass", "Returning w/o doing anything. %s is a namespace.",cl->GetName());
         return kFALSE;
      }
      Error("FillTClass", "TClass %s already initialized!", cl->GetName());
      return kFALSE;
   }
   if (gDebug > 1) Info("FillTClass","Loading TProtoClass for %s - %s",cl->GetName(),GetName());

   if (fPRealData.size() > 0) {

      // A first loop to retrieve the mother classes before starting to
      // fill this TClass instance. This is done in order to avoid recursions
      // for example in presence of daughter and mother class present in two
      // dictionaries compiled in two different libraries which are not linked
      // one with each other.
      for (auto element: fPRealData) {
         if (element.IsAClass() ) {
            if (gDebug > 1) Info("","Treating beforehand mother class %s",GetClassName(element.fClassIndex));
            TInterpreter::SuspendAutoParsing autoParseRaii(gInterpreter);

            TClass::GetClass(GetClassName(element.fClassIndex));
         }
      }
   }

   // Copy only the TClass bits.
   // not bit 13 and below and not bit 24 and above, just Bits 14 - 23
   UInt_t newbits = TestBits(0x00ffc000);
   cl->ResetBit(0x00ffc000);
   cl->SetBit(newbits);

   cl->fName  = this->fName;
   cl->fTitle = this->fTitle;
   cl->fBase = fBase;

   // fill list of data members in TClass
   cl->fData = new TListOfDataMembers(fData);

   // The TDataMember were passed along.
   fData.clear();

   // We need to fill enums one by one to initialise the internal map which is
   // transient
   {
      auto temp = new TListOfEnums();
      if (fEnums) {
         for (TObject* enumAsTObj : *fEnums){
            temp->Add((TEnum*) enumAsTObj);
         }
         // We did not transfer the container itself, let remove it from memory without deleting its content.
         fEnums->Clear();
         delete fEnums;
         fEnums = nullptr;
      }
      cl->fEnums = temp;
   }

   cl->fSizeof = fSizeof;
   cl->fCheckSum = fCheckSum;
   cl->fCanSplit = fCanSplit;
   cl->fProperty = fProperty;
   cl->fClassProperty = fClassProperty;
   cl->fStreamerType = fStreamerType;

   // Update pointers to TClass
   if (cl->fBase.load()) {
      for (auto base: *cl->fBase) {
         ((TBaseClass*)base)->SetClass(cl);
      }
   }
   if (cl->fData) {
      for (auto dm: *cl->fData) {
         ((TDataMember*)dm)->SetClass(cl);
      }
      ((TListOfDataMembers*)cl->fData)->SetClass(cl);
   }
   if (cl->fEnums.load()) {
      for (auto en: *cl->fEnums) {
         ((TEnum*)en)->SetClass(cl);
      }
      ((TListOfEnums*)cl->fEnums)->SetClass(cl);
   }


   TClass* currentRDClass = cl;
   TRealData * prevRealData = 0;
   int prevLevel = 0;
   bool first = true;
   if (fPRealData.size()  > 0) {
      for (auto element: fPRealData) {
         if (element.IsAClass() ) {
            // We now check for the TClass entry, w/o loading. Indeed we did that above.
            // If the class is not found, it means that really it was not selected and we
            // replace it with an empty placeholder with the status of kForwardDeclared.
            // Interactivity will be of course possible but if IO is attempted, a warning
            // will be issued.
            TInterpreter::SuspendAutoParsing autoParseRaii(gInterpreter);

            // Disable autoparsing which might be triggered by the use of ResolvedTypedef
            // and the fallback new TClass() below.
            currentRDClass = TClass::GetClass(GetClassName(element.fClassIndex), false /* Load */ );
            if (!currentRDClass && !element.TestFlag(TProtoRealData::kIsTransient)) {
               if (gDebug>1)
                  Info("FillTClass()",
                       "Cannot find TClass for %s; Creating an empty one in the kForwardDeclared state.",
                       GetClassName(element.fClassIndex));
               currentRDClass = new TClass(GetClassName(element.fClassIndex),1,TClass::kForwardDeclared, true /*silent*/);
            }
         }

         if (!currentRDClass) continue;
         // pass a previous real data only if depth

         if (TRealData* rd = element.CreateRealData(currentRDClass, cl,prevRealData, prevLevel)) {
            if (first) {
               //LM: need to do here because somehow fRealData is destroyed when calling TClass::GetListOfDataMembers()
               if (cl->fRealData) {
                  Info("FillTClass","Real data for class %s is not empty - make a new one",cl->GetName() );
                  delete cl->fRealData;
               }
               cl->fRealData = new TList(); // FIXME: this should really become a THashList!
               first = false;
            }

            cl->fRealData->AddLast(rd);
            prevRealData = rd;
            prevLevel = element.fLevel;

         }

      }
   }
   else {
      if (cl->fRealData) {
         Info("FillTClas","Real data for class %s is not empty - make a new one. Class has no Proto-realdata",cl->GetName() );
         delete cl->fRealData;
      }
      cl->fRealData = new TList(); // FIXME: this should really become a THashList!
   }

   cl->SetStreamerImpl();

   // set to zero in order not to delete when protoclass is deleted
   fBase = 0;
   fEnums = 0;

   fPRealData.clear();
   fPRealData.shrink_to_fit();  // to reset the underlying allocate space

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////

TProtoClass::TProtoRealData::TProtoRealData(const TRealData* rd):
   fOffset(rd->GetThisOffset()),
   fDMIndex(-1),
   fLevel(0),
   fClassIndex(-1),
   fStatusFlag(0)
{
   TDataMember * dm = rd->GetDataMember();
   TClass * cl = dm->GetClass();
   assert(cl != NULL);
   fDMIndex = DataMemberIndex(cl,dm->GetName());
   TString fullDataMemberName = rd->GetName(); // full data member name (e.g. fXaxis.fNbins)
   fLevel = fullDataMemberName.CountChar('.');

   if (fullDataMemberName.Contains("*") ) SetFlag(kIsPointer);

   // Initialize this from a TRealData object.
   SetFlag(kIsObject, rd->IsObject());
   SetFlag(kIsTransient, rd->TestBit(TRealData::kTransient) );
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor to pin vtable.
///if (gErrorIgnoreLevel==-2) printf("destroy real data %s - ",GetName());

TProtoClass::TProtoRealData::~TProtoRealData()
{
}

////////////////////////////////////////////////////////////////////////////////
/// Create a TRealData from this, with its data member coming from dmClass.
/// find data member from protoclass

TRealData* TProtoClass::TProtoRealData::CreateRealData(TClass* dmClass,
                                                       TClass* parent, TRealData *prevData, int prevLevel) const
{

   TDataMember* dm = TProtoClass::FindDataMember(dmClass, fDMIndex);

   if (!dm && dmClass->GetState()!=TClass::kForwardDeclared) {
      ::CppyyLegacy::Error("CreateRealData",
              "Cannot find data member # %d of class %s for parent %s!", fDMIndex, dmClass->GetName(),
              parent->GetName());
      return nullptr;
   }

   // here I need to re-construct the realdata full name (e.g. fAxis.fNbins)

   TString realMemberName;
   // keep an empty name if data member is not found
   if (dm) realMemberName = dm->GetName();
   if (TestFlag(kIsPointer) )
      realMemberName = TString("*")+realMemberName;
   else if (dm){
      if (dm->GetArrayDim() > 0) {
         // in case of array (like fMatrix[2][2] we need to add max index )
         // this only in case of it os not a pointer
         for (int idim = 0; idim < dm->GetArrayDim(); ++idim)
            realMemberName += TString::Format("[%d]",dm->GetMaxIndex(idim) );
      } else if (TClassEdit::IsStdArray(dm->GetTypeName())) {
         std::string typeNameBuf;
         Int_t ndim = dm->GetArrayDim();
         std::array<Int_t, 5> maxIndices; // 5 is the maximum supported in TStreamerElement::SetMaxIndex
         TClassEdit::GetStdArrayProperties(dm->GetTypeName(),
                                           typeNameBuf,
                                           maxIndices,
                                           ndim);
         for (Int_t idim = 0; idim < ndim; ++idim) {
            realMemberName += TString::Format("[%d]",maxIndices[idim] );
         }
      }
   }

   if (prevData && fLevel > 0 ) {
      if (fLevel-prevLevel == 1) // I am going down 1 level
         realMemberName = TString::Format("%s.%s",prevData->GetName(), realMemberName.Data() );
      else if (fLevel <= prevLevel) { // I am at the same level
                                      // need to strip out prev name
         std::string prevName = prevData->GetName();
         // we strip the prev data member name from the full name
         std::string parentName;
         for (int i = 0; i < prevLevel-fLevel+1; ++i) {
            parentName = prevName.substr(0, prevName.find_last_of(".") );
            prevName = parentName;
         }

         // now we need to add the current name
         realMemberName =  TString::Format("%s.%s",parentName.c_str(), realMemberName.Data() );
      }
   }

   TRealData* rd = new TRealData(realMemberName, fOffset, dm);
   if (TestFlag(kIsTransient)) {
      rd->SetBit(TRealData::kTransient);
   }
   rd->SetIsObject(TestFlag(kIsObject) );
   return rd;
}

////////////////////////////////////////////////////////////////////////////////

Int_t TProtoClass::DataMemberIndex(TClass * cl, const char * name)
{
   TList * dmList = cl->GetListOfDataMembers();

   // we cannot use IndexOf because order is guaranteed only for non-static data member
   Int_t index = 0;
   for ( auto * obj : *dmList) {
      TDataMember * dm = (TDataMember *) obj;
      if (!dm ) continue;
      if (dm->Property() & kIsStatic) continue;
      if ( TString(dm->GetName()) == TString(name) )
         return index;
      index++;
   }
   ::CppyyLegacy::Error("TProtoClass::DataMemberIndex","data member %s is not found in class %s",name, cl->GetName());
   return -1;
}
////////////////////////////////////////////////////////////////////////////////

TDataMember * TProtoClass::FindDataMember(TClass * cl, Int_t index)
{
   TList * dmList = cl->GetListOfDataMembers(false);

   // we cannot use IndexOf because order is guaranteed only for non-static data member
   Int_t i = 0;
   for ( auto * obj : *dmList) {
      TDataMember * dm = (TDataMember *) obj;
      if (!dm ) continue;
      if (dm->Property() & kIsStatic) continue;
      if (i == index)
         return dm;
      i++;
   }
   if (cl->GetState()!=TClass::kForwardDeclared)
      ::CppyyLegacy::Error("TProtoClass::FindDataMember","data member with index %d is not found in class %s",index,cl->GetName());
   return nullptr;
}

} // namespace CppyyLegacy

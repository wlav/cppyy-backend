// @(#)root/cont:$Id$
// Author: Fons Rademakers   11/08/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TObjectTable
\ingroup Containers
This class registers all instances of TObject and its derived
classes in a hash table. The Add() and Remove() members are called
from the TObject ctor and dtor, respectively.

Using the compile option R__NOSTATS one can de-active this feature
for the entire system (for maximum performance in highly time
critical applications).

*/

#include "TObjectTable.h"
#include "TROOT.h"
#include "TClass.h"
#include "TError.h"
#define Printf TStringPrintf


ClassImp(CppyyLegacy::TObjectTable);

namespace CppyyLegacy {

TObjectTable *gObjectTable;

////////////////////////////////////////////////////////////////////////////////
/// Create an object table.

TObjectTable::TObjectTable(Int_t tableSize)
{
   fSize  = (Int_t)TMath::NextPrime(tableSize);
   fTable = new TObject* [fSize];
   memset(fTable, 0, fSize*sizeof(TObject*));
   fTally = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Delete TObjectTable.

TObjectTable::~TObjectTable()
{
   delete [] fTable; fTable = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Add an object to the object table.

void TObjectTable::Add(TObject *op)
{
   if (!op) {
      Error("Add", "op is 0");
      return;
   }
   if (!fTable)
      return;

   Int_t slot = FindElement(op);
   if (fTable[slot] == 0) {
      fTable[slot] = op;
      fTally++;
      if (HighWaterMark())
         Expand(2 * fSize);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Add an object to the global object table gObjectTable. If the global
/// table does not exist create it first. This member function may only
/// be used by TObject::TObject. Use Add() to add objects to any other
/// TObjectTable object. This is a static function.

void TObjectTable::AddObj(TObject *op)
{
   static Bool_t olock = kFALSE;

   if (!op) {
      ::CppyyLegacy::Error("TObjectTable::AddObj", "op is 0");
      return;
   }
   if (olock)
      return;

   if (!gObjectTable) {
      olock = kTRUE;
      gObjectTable = new TObjectTable(10000);
      olock = kFALSE;
      gObjectTable->Add(gObjectTable);
   }

   gObjectTable->Add(op);
}

////////////////////////////////////////////////////////////////////////////////
/// Delete all objects stored in the TObjectTable.

void TObjectTable::Delete(Option_t *)
{
   for (int i = 0; i < fSize; i++) {
      if (fTable[i]) {
         delete fTable[i];
         fTable[i] = 0;
      }
   }
   fTally = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Remove an object from the object table.

void TObjectTable::Remove(TObject *op)
{
   if (op == 0) {
      Error("Remove", "remove 0 from TObjectTable");
      return;
   }

   if (!fTable)
      return;

   Int_t i = FindElement(op);
   if (fTable[i] == 0) {
      Warning("Remove", "0x%td not found at %d", (intptr_t)op, i);
      for (int j = 0; j < fSize; j++) {
         if (fTable[j] == op) {
            Error("Remove", "0x%td found at %d !!!", (intptr_t)op, j);
            i = j;
         }
      }
   }

   if (fTable[i]) {
      fTable[i] = 0;
      FixCollisions(i);
      fTally--;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Remove an object from the object table.

void TObjectTable::RemoveQuietly(TObject *op)
{
   if (op == 0) return;

   if (!fTable)
      return;

   Int_t i = FindElement(op);
   if (fTable[i] == 0)
      for (int j = 0; j < fSize; j++)
         if (fTable[j] == op)
            i = j;

   fTable[i] = 0;
   FixCollisions(i);
   fTally--;
}

////////////////////////////////////////////////////////////////////////////////
/// Deletes the object table (this static class function calls the dtor).

void TObjectTable::Terminate()
{
   delete [] fTable; fTable = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Find an object in the object table. Returns the slot where to put
/// the object. To test if the object is actually already in the table
/// use PtrIsValid().

Int_t TObjectTable::FindElement(TObject *op)
{
   Int_t    slot, n;
   TObject *slotOp;

   if (!fTable)
      return 0;

   //slot = Int_t(((ULong_t) op >> 2) % fSize);
   slot = Int_t(TString::Hash(&op, sizeof(TObject*)) % fSize);
   for (n = 0; n < fSize; n++) {
      if ((slotOp = fTable[slot]) == 0)
         break;
      if (op == slotOp)
         break;
      if (++slot == fSize)
         slot = 0;
   }
   return slot;
}

////////////////////////////////////////////////////////////////////////////////
/// Rehash the object table in case an object has been removed.

void TObjectTable::FixCollisions(Int_t index)
{
   Int_t oldIndex, nextIndex;
   TObject *nextObject;

   for (oldIndex = index+1; ;oldIndex++) {
      if (oldIndex >= fSize)
         oldIndex = 0;
      nextObject = fTable[oldIndex];
      if (nextObject == 0)
         break;
      nextIndex = FindElement(nextObject);
      if (nextIndex != oldIndex) {
         fTable[nextIndex] = nextObject;
         fTable[oldIndex] = 0;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Expand the object table.

void TObjectTable::Expand(Int_t newSize)
{
   TObject **oldTable = fTable, *op;
   int oldsize = fSize;
   newSize = (Int_t)TMath::NextPrime(newSize);
   fTable  = new TObject* [newSize];
   memset(fTable, 0, newSize*sizeof(TObject*));
   fSize   = newSize;
   fTally  = 0;
   for (int i = 0; i < oldsize; i++)
      if ((op = oldTable[i]))
         Add(op);
   delete [] oldTable;
}

////////////////////////////////////////////////////////////////////////////////
/// Issue a warning in case an object still appears in the table
/// while it should not.

void *TObjectTable::CheckPtrAndWarn(const char *msg, void *vp)
{
   if (fTable && vp && fTable[FindElement((TObject*)vp)]) {
      Remove((TObject*)vp);
      Warning("CheckPtrAndWarn", "%s (0x%td)\n", msg, (intptr_t)vp);
   }
   return vp;
}

} // namespace CppyyLegacy

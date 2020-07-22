// @(#)root/base:$Id$
// Author: Fons Rademakers   29/07/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TStorage
\ingroup Base

Storage manager. The storage manager works best in conjunction with
the custom ROOT new and delete operators defined in the file
NewDelete.cxx (libNew.so).
Memory checking is by default enabled (when using libNew.so) and
usage statistics is gathered. Using the resource (in .rootrc):
Root.MemStat one can toggle statistics gathering on or off. More
specifically on can trap the allocation of a block of memory of a
certain size. This can be specified using the resource:
Root.MemStat.size, using the resource Root.MemStat.cnt one can
specify after how many allocations of this size the trap should
occur.

Set the compile option R__NOSTATS to de-activate all memory checking
and statistics gathering in the system.
*/

#include <stdlib.h>

#include "TROOT.h"
#include "TObjectTable.h"
#include "TError.h"
#include "TString.h"
#include "TVirtualMutex.h"
#include "TInterpreter.h"

#define R__NOSTATS 1

#if !defined(R__NOSTATS)
#   define MEM_DEBUG
#   define MEM_STAT
#   define MEM_CHECKOBJECTPOINTERS
#endif

#if defined(MEM_STAT) && !defined(MEM_DEBUG)
#   define MEM_DEBUG
#endif

#ifdef MEM_DEBUG
#   ifdef R__B64
#      define storage_size(p) ((size_t)(((size_t*)p)[-1]))
#   else
#      define storage_size(p) ((size_t)(((int*)p)[-2]))
#   endif
#else
#   define storage_size(p) ((size_t)0)
#endif

#define PVOID (-1)
#define Printf TStringPrintf


ClassImp(CppyyLegacy::TStorage);

namespace CppyyLegacy {

size_t        TStorage::fgMaxBlockSize;
FreeHookFun_t TStorage::fgFreeHook;
void         *TStorage::fgFreeHookData;
ReAllocFun_t  TStorage::fgReAllocHook;
ReAllocCFun_t TStorage::fgReAllocCHook;
Bool_t        TStorage::fgHasCustomNewDelete;

//------------------------------------------------------------------------------

static const char *gSpaceErr = "storage exhausted";

const size_t kObjMaxSize = 10024;

////////////////////////////////////////////////////////////////////////////////
/// Allocate a block of memory, that later can be resized using
/// TStorage::ReAlloc().

void *TStorage::Alloc(size_t size)
{
   static const char *where = "TStorage::Alloc";

#ifndef WIN32
   void *vp = ::operator new[](size);
#else
   void *vp = ::operator new(size);
#endif
   if (vp == 0)
      Fatal(where, "%s", gSpaceErr);

   return vp;
}

////////////////////////////////////////////////////////////////////////////////
/// De-allocate block of memory, that was allocated via TStorage::Alloc().

void TStorage::Dealloc(void *ptr)
{
#ifndef WIN32
   ::operator delete[](ptr);
#else
   ::operator delete(ptr);
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Reallocate (i.e. resize) block of memory. Checks if current size is
/// equal to oldsize. If not memory was overwritten.

void *TStorage::ReAlloc(void *ovp, size_t size, size_t oldsize)
{
   static const char *where = "TStorage::ReAlloc";

   if (oldsize == size)
      return ovp;

#ifndef WIN32
   void *vp = ::operator new[](size);
#else
   void *vp = ::operator new(size);
#endif
   if (vp == 0)
      Fatal(where, "%s", gSpaceErr);

   if (ovp == 0)
      return vp;

   if (size > oldsize) {
      memcpy(vp, ovp, oldsize);
      memset((char*)vp+oldsize, 0, size-oldsize);
   } else
      memcpy(vp, ovp, size);
#ifndef WIN32
   ::operator delete[](ovp);
#else
   ::operator delete(ovp);
#endif
   return vp;
}

////////////////////////////////////////////////////////////////////////////////
/// Reallocate (i.e. resize) array of chars. Size and oldsize are
/// in number of chars.

char *TStorage::ReAllocChar(char *ovp, size_t size, size_t oldsize)
{
   static const char *where = "TStorage::ReAllocChar";

   char *vp;
   if (ovp == 0) {
      vp = new char[size];
      if (vp == 0)
         Fatal(where, "%s", gSpaceErr);
      return vp;
   }
   if (oldsize == size)
      return ovp;

   vp = new char[size];
   if (vp == 0)
      Fatal(where, "%s", gSpaceErr);
   if (size > oldsize) {
      memcpy(vp, ovp, oldsize);
      memset((char*)vp+oldsize, 0, size-oldsize);
   } else
      memcpy(vp, ovp, size);
   delete [] ovp;
   return vp;
}

////////////////////////////////////////////////////////////////////////////////
/// Reallocate (i.e. resize) array of integers. Size and oldsize are
/// number of integers (not number of bytes).

Int_t *TStorage::ReAllocInt(Int_t *ovp, size_t size, size_t oldsize)
{
   static const char *where = "TStorage::ReAllocInt";

   Int_t *vp;
   if (ovp == 0) {
      vp = new Int_t[size];
      if (vp == 0)
         Fatal(where, "%s", gSpaceErr);
      return vp;
   }
   if (oldsize == size)
      return ovp;

   vp = new Int_t[size];
   if (vp == 0)
      Fatal(where, "%s", gSpaceErr);
   if (size > oldsize) {
      memcpy(vp, ovp, oldsize*sizeof(Int_t));
      memset((Int_t*)vp+oldsize, 0, (size-oldsize)*sizeof(Int_t));
   } else
      memcpy(vp, ovp, size*sizeof(Int_t));
   delete [] ovp;
   return vp;
}

////////////////////////////////////////////////////////////////////////////////
/// Used to allocate a TObject on the heap (via TObject::operator new()).
/// Directly after this routine one can call (in the TObject ctor)
/// TStorage::FilledByObjectAlloc() to find out if the just created object is on
/// the heap.  This technique is necessary as there is one stack per thread
/// and we can not rely on comparison with the current stack memory position.

void *TStorage::ObjectAlloc(size_t sz)
{
   void* space =  ::operator new(sz);
   memset(space, kObjectAllocMemValue, sz);
   return space;
}

////////////////////////////////////////////////////////////////////////////////
/// Used to allocate array of TObject on the heap (via TObject::operator new[]()).
/// Unlike the 'singular' ObjectAlloc, we do not mark those object has being
/// allocated on the heap as they can not be individually deleted.

void *TStorage::ObjectAllocArray(size_t sz)
{
   void* space =  ::operator new(sz);
   return space;
}

////////////////////////////////////////////////////////////////////////////////
/// Used to allocate a TObject on the heap (via TObject::operator new(size_t,void*))
/// in position vp. vp is already allocated (maybe on heap, maybe on
/// stack) so just return.

void *TStorage::ObjectAlloc(size_t , void *vp)
{
   return vp;
}

////////////////////////////////////////////////////////////////////////////////
/// Used to deallocate a TObject on the heap (via TObject::operator delete()).

void TStorage::ObjectDealloc(void *vp)
{

   ::operator delete(vp);
}

////////////////////////////////////////////////////////////////////////////////
/// Used to deallocate a TObject on the heap (via TObject::operator delete(void*,void*)).

void TStorage::ObjectDealloc(void *vp, void *ptr)
{
   if (vp && ptr) { }
}

#ifdef R__SIZEDDELETE
////////////////////////////////////////////////////////////////////////////////
/// Used to deallocate a TObject on the heap (via TObject::operator delete()),
/// for sized deallocation.

void TStorage::ObjectDealloc(void *vp, size_t size)
{
   ::operator delete(vp, size);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// Set a free handler.

void TStorage::SetFreeHook(FreeHookFun_t fh, void *data)
{
   fgFreeHook     = fh;
   fgFreeHookData = data;
}

////////////////////////////////////////////////////////////////////////////////
/// Set a custom ReAlloc handlers. This function is typically
/// called via a static object in the ROOT libNew.so shared library.

void TStorage::SetReAllocHooks(ReAllocFun_t rh1, ReAllocCFun_t rh2)
{
   fgReAllocHook  = rh1;
   fgReAllocCHook = rh2;
}

////////////////////////////////////////////////////////////////////////////////
///return static free hook data

void *TStorage::GetFreeHookData()
{
   return fgFreeHookData;
}

////////////////////////////////////////////////////////////////////////////////
///return the has custom delete flag

Bool_t TStorage::HasCustomNewDelete()
{
   return fgHasCustomNewDelete;
}

////////////////////////////////////////////////////////////////////////////////
///set the has custom delete flag

void TStorage::SetCustomNewDelete()
{
   fgHasCustomNewDelete = kTRUE;
}

} // namespace CppyyLegacy

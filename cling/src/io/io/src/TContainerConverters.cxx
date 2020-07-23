// @(#)root/io:$Id: 56ae10c519627872e1dd40872fd459c2dd89acf6 $
// Author: Philippe Canal  11/11/2004

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Helper classes to convert collection from ROOT collection to STL     //
// collections                                                          //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TContainerConverters.h"
#include "TStreamerInfo.h"
#include "TVirtualCollectionProxy.h"
#include "TError.h"
#include "TGenCollectionStreamer.h"
#include "TClassStreamer.h"
#include <stdlib.h>


namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// Constructor.

TConvertMapToProxy::TConvertMapToProxy(TClassStreamer *streamer,
                                       Bool_t isPointer, Bool_t isPrealloc) :
   fIsPointer(isPointer),
   fIsPrealloc(isPrealloc),
   fSizeOf(0),
   fCollectionClass(0)
{
   TCollectionClassStreamer *middleman = dynamic_cast<TCollectionClassStreamer*>(streamer);
   if (middleman) {
      TVirtualCollectionProxy *proxy = middleman->GetXYZ();
      TGenCollectionStreamer *collStreamer = dynamic_cast<TGenCollectionStreamer*>(proxy);

      fCollectionClass = proxy->GetCollectionClass();

      if (isPointer) fSizeOf = sizeof(void*);
      else fSizeOf = fCollectionClass->Size();

      if (proxy->GetValueClass()->GetStreamerInfo() == 0
          || proxy->GetValueClass()->GetStreamerInfo()->GetElements()->At(1) == 0 ) {
         // We do not have enough information on the pair (or its not a pair).
         collStreamer = 0;
      }
      if (!collStreamer) fCollectionClass = 0;
   }
}



////////////////////////////////////////////////////////////////////////////////
/// Read a std::map or std::multimap from the TBuffer b and load it into a (stl) collection

void TConvertMapToProxy::operator()(TBuffer &b, void *pmember, Int_t size)
{
   R__ASSERT(b.IsReading());
   R__ASSERT(fCollectionClass);

   // For thread-safety we need to go through TClass::GetStreamer
   // to get a thread local proxy.
   TCollectionClassStreamer *middleman = dynamic_cast<TCollectionClassStreamer*>(fCollectionClass->GetStreamer());
   TVirtualCollectionProxy *proxy = middleman->GetXYZ();
   TGenCollectionStreamer *collStreamer = dynamic_cast<TGenCollectionStreamer*>(proxy);

   Bool_t needAlloc = fIsPointer && !fIsPrealloc;

   R__ASSERT(!needAlloc); // not yet implemented

   if (needAlloc) {
      char *addr = (char*)pmember;
      for(Int_t k=0; k<size; ++k, addr += fSizeOf ) {
         if (*(void**)addr && TStreamerInfo::CanDelete()) {
            proxy->GetValueClass()->Destructor(*(void**)addr,kFALSE); // call delete and desctructor
         }
      }
   }


   char *addr = (char*)pmember;
   if (size==0) size=1;
   for(Int_t k=0; k<size; ++k, addr += fSizeOf) {

      if (needAlloc) {

         // Read the class name.

      }

      void *obj;
      if (fIsPointer) obj = *(void**)addr;
      else obj = addr;

      TVirtualCollectionProxy::TPushPop env(proxy, obj);
      collStreamer->StreamerAsMap(b);

   }
}

} // namespace CppyyLegacy

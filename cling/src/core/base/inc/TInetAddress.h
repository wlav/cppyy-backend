// @(#)root/base:$Id$
// Author: Fons Rademakers   16/12/96

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TInetAddress
#define ROOT_TInetAddress


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TInetAddress                                                         //
//                                                                      //
// This class represents an Internet Protocol (IP) address.             //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TObject.h"
#include "TString.h"

#include <vector>
#ifdef R__GLOBALSTL
namespace std { using ::vector; }
#endif


namespace CppyyLegacy {

class TInetAddress : public TObject {

friend class TSystem;
friend class TUnixSystem;
friend class TWinNTSystem;
friend class TUUID;

public:
   typedef std::vector<UInt_t>  AddressList_t;
   typedef std::vector<TString> AliasList_t;

private:
   TString       fHostname;    // fully qualified hostname
   Int_t         fFamily;      // address family
   Int_t         fPort;        // port through which we are connected
   AddressList_t fAddresses;   // list of all IP addresses in host byte order
   AliasList_t   fAliases;     // list of aliases

   TInetAddress(const char *host, UInt_t addr, Int_t family, Int_t port = -1);
   void AddAddress(UInt_t addr);
   void AddAlias(const char *alias);

public:
   TInetAddress();
   TInetAddress(const TInetAddress &adr);
   TInetAddress &operator=(const TInetAddress &rhs);
   virtual ~TInetAddress() { }

   UInt_t      GetAddress() const { return fAddresses[0]; }
   UChar_t    *GetAddressBytes() const;
   const char *GetHostAddress() const;
   const char *GetHostName() const { return (const char *) fHostname; }
   Int_t       GetFamily() const { return fFamily; }
   Int_t       GetPort() const { return fPort; }
   const AddressList_t &GetAddresses() const { return fAddresses; }
   const AliasList_t   &GetAliases() const { return fAliases; }
   Bool_t      IsValid() const { return fFamily == -1 ? kFALSE : kTRUE; }

   static const char *GetHostAddress(UInt_t addr);

   ClassDef(TInetAddress,4)  //Represents an Internet Protocol (IP) address
};

} // namespace CppyyLegacy

#endif

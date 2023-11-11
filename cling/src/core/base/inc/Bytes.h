/* @(#)root/base:$Id$ */

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_Bytes
#define ROOT_Bytes


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Bytes                                                                //
//                                                                      //
// A set of inline byte handling routines.                              //
//                                                                      //
// The set of tobuf() and frombuf() routines take care of packing a     //
// basic type value into a buffer in network byte order (i.e. they      //
// perform byte swapping when needed). The buffer does not have to      //
// start on a machine (long) word boundary.                             //
//                                                                      //
// For __GNUC__ on linux on i486 processors and up                      //
// use the `bswap' opcode provided by the GNU C Library.                //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "Rtypes.h"

#ifndef __CINT__
#include <string.h>
#endif

#if (defined(__linux) || defined(__APPLE__)) && \
    (defined(__i386__) || defined(__x86_64__)) && \
     defined(__GNUC__)
#define R__USEASMSWAP
#endif

//Big bug in inline byte swapping code with Intel's icc
#if defined(__INTEL_COMPILER) && __INTEL_COMPILER < 1000
#undef R__USEASMSWAP
#endif

#if defined(R__USEASMSWAP) && !defined(__CINT__)
#include "Byteswap.h"
#endif

namespace CppyyLegacy {

//______________________________________________________________________________
inline void tobuf(char *&buf, Bool_t x)
{
   UChar_t x1 = x;
   *buf++ = x1;
}

inline void tobuf(char *&buf, UChar_t x)
{
   *buf++ = x;
}

inline void tobuf(char *&buf, UShort_t x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   *((UShort_t *)buf) = Rbswap_16(x);
# else
   // To work around a stupid optimization bug in MSVC++ 6.0
   const UShort_t *intermediary = &x;
   char *sw = (char *) intermediary;
   buf[0] = sw[1];
   buf[1] = sw[0];
# endif
#else
   memcpy(buf, &x, sizeof(UShort_t));
#endif
   buf += sizeof(UShort_t);
}

inline void tobuf(char *&buf, UInt_t x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   *((UInt_t *)buf) = Rbswap_32(x);
# else
   // To work around a stupid optimization bug in MSVC++ 6.0
   const UInt_t *intermediary = &x;
   char *sw = (char *)intermediary;
   buf[0] = sw[3];
   buf[1] = sw[2];
   buf[2] = sw[1];
   buf[3] = sw[0];
# endif
#else
   memcpy(buf, &x, sizeof(UInt_t));
#endif
   buf += sizeof(UInt_t);
}

inline void tobuf(char *&buf, ULong_t x)
{
#ifdef R__BYTESWAP
   // To work around a stupid optimization bug in MSVC++ 6.0
   const ULong_t *intermediary = &x;
   char *sw = (char *)intermediary;
   if (sizeof(ULong_t) == 8) {
      buf[0] = sw[7];
      buf[1] = sw[6];
      buf[2] = sw[5];
      buf[3] = sw[4];
      buf[4] = sw[3];
      buf[5] = sw[2];
      buf[6] = sw[1];
      buf[7] = sw[0];
   } else {
      buf[0] = 0;
      buf[1] = 0;
      buf[2] = 0;
      buf[3] = 0;
      buf[4] = sw[3];
      buf[5] = sw[2];
      buf[6] = sw[1];
      buf[7] = sw[0];
   }
#else
   if (sizeof(ULong_t) == 8) {
      memcpy(buf, &x, 8);
   } else {
      buf[0] = 0;
      buf[1] = 0;
      buf[2] = 0;
      buf[3] = 0;
      memcpy(buf+4, &x, 4);
   }
#endif
   buf += 8;
}

inline void tobuf(char *&buf, Long_t x)
{
#ifdef R__BYTESWAP
   // To work around a stupid optimization bug in MSVC++ 6.0
   const Long_t *intermediary = &x;
   char *sw = (char *)intermediary;
   if (sizeof(Long_t) == 8) {
      buf[0] = sw[7];
      buf[1] = sw[6];
      buf[2] = sw[5];
      buf[3] = sw[4];
      buf[4] = sw[3];
      buf[5] = sw[2];
      buf[6] = sw[1];
      buf[7] = sw[0];
   } else {
      if (x < 0) {
         buf[0] = (char) -1;
         buf[1] = (char) -1;
         buf[2] = (char) -1;
         buf[3] = (char) -1;
      } else {
         buf[0] = 0;
         buf[1] = 0;
         buf[2] = 0;
         buf[3] = 0;
      }
      buf[4] = sw[3];
      buf[5] = sw[2];
      buf[6] = sw[1];
      buf[7] = sw[0];
   }
#else
   if (sizeof(Long_t) == 8) {
      memcpy(buf, &x, 8);
   } else {
      if (x < 0) {
         buf[0] = (char) -1;
         buf[1] = (char) -1;
         buf[2] = (char) -1;
         buf[3] = (char) -1;
      } else {
         buf[0] = 0;
         buf[1] = 0;
         buf[2] = 0;
         buf[3] = 0;
      }
      memcpy(buf+4, &x, 4);
   }
#endif
   buf += 8;
}

inline void tobuf(char *&buf, ULong64_t x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   *((ULong64_t *)buf) = Rbswap_64(x);
# else
   // To work around a stupid optimization bug in MSVC++ 6.0
   const ULong64_t *intermediary = &x;
   char *sw = (char *)intermediary;
   buf[0] = sw[7];
   buf[1] = sw[6];
   buf[2] = sw[5];
   buf[3] = sw[4];
   buf[4] = sw[3];
   buf[5] = sw[2];
   buf[6] = sw[1];
   buf[7] = sw[0];
# endif
#else
   memcpy(buf, &x, sizeof(ULong64_t));
#endif
   buf += sizeof(ULong64_t);
}

inline void tobuf(char *&buf, Float_t x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   union {
      volatile UInt_t  i;
      volatile Float_t f;
   } u;
   u.f = x;
   *((UInt_t *)buf) = Rbswap_32(u.i);
# else
   union {
      volatile char    c[4];
      volatile Float_t f;
   } u;
   u.f = x;
   buf[0] = u.c[3];
   buf[1] = u.c[2];
   buf[2] = u.c[1];
   buf[3] = u.c[0];
# endif
#else
   memcpy(buf, &x, sizeof(Float_t));
#endif
   buf += sizeof(Float_t);
}

inline void tobuf(char *&buf, Double_t x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   union {
      volatile ULong64_t l;
      volatile Double_t  d;
   } u;
   u.d = x;
   *((ULong64_t *)buf) = Rbswap_64(u.l);
# else
   union {
      volatile char     c[8];
      volatile Double_t d;
   } u;
   u.d = x;
   buf[0] = u.c[7];
   buf[1] = u.c[6];
   buf[2] = u.c[5];
   buf[3] = u.c[4];
   buf[4] = u.c[3];
   buf[5] = u.c[2];
   buf[6] = u.c[1];
   buf[7] = u.c[0];
# endif
#else
   memcpy(buf, &x, sizeof(Double_t));
#endif
   buf += sizeof(Double_t);
}

inline void frombuf(char *&buf, Bool_t *x)
{
   UChar_t x1;
   x1 = *buf++;
   *x = (Bool_t) (x1 != 0);
}

inline void frombuf(char *&buf, UChar_t *x)
{
   *x = *buf++;
}

inline void frombuf(char *&buf, UShort_t *x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   *x = Rbswap_16(*((UShort_t *)buf));
# else
   char *sw = (char *)x;
   sw[0] = buf[1];
   sw[1] = buf[0];
# endif
#else
   memcpy(x, buf, sizeof(UShort_t));
#endif
   buf += sizeof(UShort_t);
}

inline void frombuf(char *&buf, UInt_t *x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   *x = Rbswap_32(*((UInt_t *)buf));
# else
   char *sw = (char *)x;
   sw[0] = buf[3];
   sw[1] = buf[2];
   sw[2] = buf[1];
   sw[3] = buf[0];
# endif
#else
   memcpy(x, buf, sizeof(UInt_t));
#endif
   buf += sizeof(UInt_t);
}

inline void frombuf(char *&buf, ULong_t *x)
{
#ifdef R__BYTESWAP
   char *sw = (char *)x;
   if (sizeof(ULong_t) == 8) {
      sw[0] = buf[7];
      sw[1] = buf[6];
      sw[2] = buf[5];
      sw[3] = buf[4];
      sw[4] = buf[3];
      sw[5] = buf[2];
      sw[6] = buf[1];
      sw[7] = buf[0];
   } else {
      sw[0] = buf[7];
      sw[1] = buf[6];
      sw[2] = buf[5];
      sw[3] = buf[4];
   }
#else
   if (sizeof(ULong_t) == 8) {
      memcpy(x, buf, 8);
   } else {
      memcpy(x, buf+4, 4);
   }
#endif
   buf += 8;
}

inline void frombuf(char *&buf, ULong64_t *x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   *x = Rbswap_64(*((ULong64_t *)buf));
# else
   char *sw = (char *)x;
   sw[0] = buf[7];
   sw[1] = buf[6];
   sw[2] = buf[5];
   sw[3] = buf[4];
   sw[4] = buf[3];
   sw[5] = buf[2];
   sw[6] = buf[1];
   sw[7] = buf[0];
# endif
#else
   memcpy(x, buf, sizeof(ULong64_t));
#endif
   buf += sizeof(ULong64_t);
}

inline void frombuf(char *&buf, Float_t *x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   // Use a union to allow strict-aliasing
   union {
      volatile UInt_t  i;
      volatile Float_t f;
   } u;
   u.i = Rbswap_32(*((UInt_t *)buf));
   *x = u.f;
# else
   union {
      volatile char    c[4];
      volatile Float_t f;
   } u;
   u.c[0] = buf[3];
   u.c[1] = buf[2];
   u.c[2] = buf[1];
   u.c[3] = buf[0];
   *x = u.f;
# endif
#else
   memcpy(x, buf, sizeof(Float_t));
#endif
   buf += sizeof(Float_t);
}

inline void frombuf(char *&buf, Double_t *x)
{
#ifdef R__BYTESWAP
# if defined(R__USEASMSWAP)
   // Use a union to allow strict-aliasing
   union {
      volatile ULong64_t l;
      volatile Double_t  d;
   } u;
   u.l = Rbswap_64(*((ULong64_t *)buf));
   *x = u.d;
# else
   union {
      volatile char     c[8];
      volatile Double_t d;
   } u;
   u.c[0] = buf[7];
   u.c[1] = buf[6];
   u.c[2] = buf[5];
   u.c[3] = buf[4];
   u.c[4] = buf[3];
   u.c[5] = buf[2];
   u.c[6] = buf[1];
   u.c[7] = buf[0];
   *x = u.d;
# endif
#else
   memcpy(x, buf, sizeof(Double_t));
#endif
   buf += sizeof(Double_t);
}

inline void tobuf(char *&buf, Char_t x)   { tobuf(buf, (UChar_t) x); }
inline void tobuf(char *&buf, Short_t x)  { tobuf(buf, (UShort_t) x); }
inline void tobuf(char *&buf, Int_t x)    { tobuf(buf, (UInt_t) x); }
inline void tobuf(char *&buf, Long64_t x) { tobuf(buf, (ULong64_t) x); }

inline void frombuf(char *&buf, Char_t *x)   { frombuf(buf, (UChar_t *) x); }
inline void frombuf(char *&buf, Short_t *x)  { frombuf(buf, (UShort_t *) x); }
inline void frombuf(char *&buf, Int_t *x)    { frombuf(buf, (UInt_t *) x); }
inline void frombuf(char *&buf, Long_t *x)   { frombuf(buf, (ULong_t *) x); }
inline void frombuf(char *&buf, Long64_t *x) { frombuf(buf, (ULong64_t *) x); }

} // namespace CppyyLegacy

#endif

// @(#)root/base:
// Authors: Rene Brun, Fons Rademakers   29/07/95

/*************************************************************************
 * Copyright (C) 1995-2004, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TMathBase
#define ROOT_TMathBase


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TMath Base functions                                                 //
//                                                                      //
// Define the functions Min, Max, Abs, Sign, Range for all types.       //
// NB: These functions are unfortunately not available in a portable    //
// way in std::.                                                        //
//                                                                      //
// More functions are defined in TMath.h. TMathBase.h is designed to be //
// a stable file and used in place of TMath.h in the ROOT miniCore.     //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "Rtypes.h"

#include <cstdlib>
#include <cmath>
#include <algorithm>


namespace CppyyLegacy {

namespace TMath {

   // Abs
   inline Short_t  Abs(Short_t d);
   inline Int_t    Abs(Int_t d);
   inline Long_t   Abs(Long_t d);
   inline Long64_t Abs(Long64_t d);
   inline Float_t  Abs(Float_t d);
   inline Double_t Abs(Double_t d);
   inline LongDouble_t Abs(LongDouble_t d);

   // Min, Max of two scalars
   inline Short_t   Min(Short_t a, Short_t b);
   inline UShort_t  Min(UShort_t a, UShort_t b);
   inline Int_t     Min(Int_t a, Int_t b);
   inline UInt_t    Min(UInt_t a, UInt_t b);
   inline Long_t    Min(Long_t a, Long_t b);
   inline ULong_t   Min(ULong_t a, ULong_t b);
   inline Long64_t  Min(Long64_t a, Long64_t b);
   inline ULong64_t Min(ULong64_t a, ULong64_t b);
   inline Float_t   Min(Float_t a, Float_t b);
   inline Double_t  Min(Double_t a, Double_t b);

   inline Short_t   Max(Short_t a, Short_t b);
   inline UShort_t  Max(UShort_t a, UShort_t b);
   inline Int_t     Max(Int_t a, Int_t b);
   inline UInt_t    Max(UInt_t a, UInt_t b);
   inline Long_t    Max(Long_t a, Long_t b);
   inline ULong_t   Max(ULong_t a, ULong_t b);
   inline Long64_t  Max(Long64_t a, Long64_t b);
   inline ULong64_t Max(ULong64_t a, ULong64_t b);
   inline Float_t   Max(Float_t a, Float_t b);
   inline Double_t  Max(Double_t a, Double_t b);

   // Range
   inline Short_t   Range(Short_t lb, Short_t ub, Short_t x);
   inline Int_t     Range(Int_t lb, Int_t ub, Int_t x);
   inline Long_t    Range(Long_t lb, Long_t ub, Long_t x);
   inline ULong_t   Range(ULong_t lb, ULong_t ub, ULong_t x);
   inline Double_t  Range(Double_t lb, Double_t ub, Double_t x);

   //NextPrime is used by the Core classes.
   Long_t   NextPrime(Long_t x);   // Least prime number greater than x

   // Sorting
   template <typename Element, typename Index>
   void Sort(Index n, const Element* a, Index* index, Bool_t down=kTRUE);
   template <typename Iterator, typename IndexIterator>
   void SortItr(Iterator first, Iterator last, IndexIterator index, Bool_t down=kTRUE);
}


//---- Abs ---------------------------------------------------------------------

inline Short_t TMath::Abs(Short_t d)
{ return (d >= 0) ? d : Short_t(-d);  }

inline Int_t TMath::Abs(Int_t d)
{ return std::abs(d); }

inline Long_t TMath::Abs(Long_t d)
{ return std::labs(d); }

inline Long64_t TMath::Abs(Long64_t d)
#if __cplusplus >= 201103
{ return std::llabs(d); }
#else
{ return (d >= 0) ? d : -d;  }
#endif

inline Float_t TMath::Abs(Float_t d)
{ return std::abs(d); }

inline Double_t TMath::Abs(Double_t d)
{ return std::abs(d); }

inline LongDouble_t TMath::Abs(LongDouble_t d)
{ return std::abs(d); }


//---- Min ---------------------------------------------------------------------

inline Short_t TMath::Min(Short_t a, Short_t b)
   { return a <= b ? a : b; }

inline UShort_t TMath::Min(UShort_t a, UShort_t b)
   { return a <= b ? a : b; }

inline Int_t TMath::Min(Int_t a, Int_t b)
   { return a <= b ? a : b; }

inline UInt_t TMath::Min(UInt_t a, UInt_t b)
   { return a <= b ? a : b; }

inline Long_t TMath::Min(Long_t a, Long_t b)
   { return a <= b ? a : b; }

inline ULong_t TMath::Min(ULong_t a, ULong_t b)
   { return a <= b ? a : b; }

inline Long64_t TMath::Min(Long64_t a, Long64_t b)
   { return a <= b ? a : b; }

inline ULong64_t TMath::Min(ULong64_t a, ULong64_t b)
   { return a <= b ? a : b; }

inline Float_t TMath::Min(Float_t a, Float_t b)
   { return a <= b ? a : b; }

inline Double_t TMath::Min(Double_t a, Double_t b)
   { return a <= b ? a : b; }

//---- Max ---------------------------------------------------------------------

inline Short_t TMath::Max(Short_t a, Short_t b)
   { return a >= b ? a : b; }

inline UShort_t TMath::Max(UShort_t a, UShort_t b)
   { return a >= b ? a : b; }

inline Int_t TMath::Max(Int_t a, Int_t b)
   { return a >= b ? a : b; }

inline UInt_t TMath::Max(UInt_t a, UInt_t b)
   { return a >= b ? a : b; }

inline Long_t TMath::Max(Long_t a, Long_t b)
   { return a >= b ? a : b; }

inline ULong_t TMath::Max(ULong_t a, ULong_t b)
   { return a >= b ? a : b; }

inline Long64_t TMath::Max(Long64_t a, Long64_t b)
   { return a >= b ? a : b; }

inline ULong64_t TMath::Max(ULong64_t a, ULong64_t b)
   { return a >= b ? a : b; }

inline Float_t TMath::Max(Float_t a, Float_t b)
   { return a >= b ? a : b; }

inline Double_t TMath::Max(Double_t a, Double_t b)
   { return a >= b ? a : b; }

//---- Range -------------------------------------------------------------------

inline Short_t TMath::Range(Short_t lb, Short_t ub, Short_t x)
   { return x < lb ? lb : (x > ub ? ub : x); }

inline Int_t TMath::Range(Int_t lb, Int_t ub, Int_t x)
   { return x < lb ? lb : (x > ub ? ub : x); }

inline Long_t TMath::Range(Long_t lb, Long_t ub, Long_t x)
   { return x < lb ? lb : (x > ub ? ub : x); }

inline ULong_t TMath::Range(ULong_t lb, ULong_t ub, ULong_t x)
   { return x < lb ? lb : (x > ub ? ub : x); }

inline Double_t TMath::Range(Double_t lb, Double_t ub, Double_t x)
   { return x < lb ? lb : (x > ub ? ub : x); }

template<typename T>
struct CompareDesc {

   CompareDesc(T d) : fData(d) {}

   template<typename Index>
   bool operator()(Index i1, Index i2) {
      return *(fData + i1) > *(fData + i2);
   }

   T fData;
};

template<typename T>
struct CompareAsc {

   CompareAsc(T d) : fData(d) {}

   template<typename Index>
   bool operator()(Index i1, Index i2) {
      return *(fData + i1) < *(fData + i2);
   }

   T fData;
};

template <typename Iterator, typename IndexIterator>
void TMath::SortItr(Iterator first, Iterator last, IndexIterator index, Bool_t down)
{
   // Sort the n1 elements of the Short_t array defined by its
   // iterators.  In output the array index contains the indices of
   // the sorted array.  If down is false sort in increasing order
   // (default is decreasing order).

   // NOTE that the array index must be created with a length bigger
   // or equal than the main array before calling this function.

   int i = 0;

   IndexIterator cindex = index;
   for ( Iterator cfirst = first; cfirst != last; ++cfirst )
   {
      *cindex = i++;
      ++cindex;
   }

   if ( down )
      std::sort(index, cindex, CompareDesc<Iterator>(first) );
   else
      std::sort(index, cindex, CompareAsc<Iterator>(first) );
}

template <typename Element, typename Index> void TMath::Sort(Index n, const Element* a, Index* index, Bool_t down)
{
   // Sort the n elements of the  array a of generic templated type Element.
   // In output the array index of type Index contains the indices of the sorted array.
   // If down is false sort in increasing order (default is decreasing order).

   // NOTE that the array index must be created with a length >= n
   // before calling this function.
   // NOTE also that the size type for n must be the same type used for the index array
   // (templated type Index)

   for(Index i = 0; i < n; i++) { index[i] = i; }
   if ( down )
      std::sort(index, index + n, CompareDesc<const Element*>(a) );
   else
      std::sort(index, index + n, CompareAsc<const Element*>(a) );
}

} // namespace CppyyLegacy

#endif

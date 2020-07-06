// root/core/meta
// vim: sw=3
// Author: Paul Russo   30/07/2012

/*************************************************************************
 * Copyright (C) 1995-2013, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_CallFunc
#define ROOT_CallFunc

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TClingCallFunc                                                       //
//                                                                      //
// Emulation of the CINT CallFunc class.                                //
//                                                                      //
// The CINT C++ interpreter provides an interface for calling           //
// functions through the generated wrappers in dictionaries with        //
// the CallFunc class. This class provides the same functionality,      //
// using an interface as close as possible to CallFunc but the          //
// function metadata and calling service comes from the Cling           //
// C++ interpreter and the Clang C++ compiler, not CINT.                //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TClingMethodInfo.h"
#include "TClingClassInfo.h"
#include "TInterpreter.h"

#include "cling/Interpreter/Value.h"

#include <llvm/ADT/SmallVector.h>


namespace clang {
class BuiltinType;
class Expr;
class FunctionDecl;
class CXXMethodDecl;
}

namespace cling {
class Interpreter;
}

class TClingClassInfo;
namespace CppyyLegacy {
   class TInterpreterValue;
}


namespace CppyyLegacy {

typedef void (*tcling_callfunc_Wrapper_t)(void*, int, void**, void*);
typedef void (*tcling_callfunc_ctor_Wrapper_t)(void**, void*, unsigned long);
typedef void (*tcling_callfunc_dtor_Wrapper_t)(void*, unsigned long, int);

class TClingCallFunc {

private:

   /// Cling interpreter, we do *not* own.
   cling::Interpreter* fInterp;
   /// ROOT normalized context for that interpreter
   const CppyyLegacy::TMetaUtils::TNormalizedCtxt &fNormCtxt;
   /// Current method, we own.
   std::unique_ptr<TClingMethodInfo> fMethod;
   /// Decl for the method
   const clang::FunctionDecl *fDecl = nullptr;
   /// Number of required arguments
   size_t fMinRequiredArguments = -1;
   /// Pointer to compiled wrapper, we do *not* own.
   tcling_callfunc_Wrapper_t fWrapper;

private:
   enum EReferenceType {
      kNotReference,
      kLValueReference,
      kRValueReference
   };

   void* compile_wrapper(const std::string& wrapper_name,
                         const std::string& wrapper,
                         bool withAccessControl = true);

   void collect_type_info(clang::QualType& QT, std::ostringstream& typedefbuf,
                          std::ostringstream& callbuf, std::string& type_name,
                          EReferenceType& refType, bool& isPointer, int indent_level,
                          bool forArgument);

   void make_narg_call(const std::string &return_type, const unsigned N, std::ostringstream &typedefbuf,
                       std::ostringstream &callbuf, const std::string &class_name, int indent_level);

   void make_narg_ctor(const unsigned N, std::ostringstream& typedefbuf,
                       std::ostringstream& callbuf,
                       const std::string& class_name, int indent_level);

   void make_narg_call_with_return(const unsigned N,
                                   const std::string& class_name,
                                   std::ostringstream& buf, int indent_level);

   void make_narg_ctor_with_return(const unsigned N,
                                   const std::string& class_name,
                                   std::ostringstream& buf, int indent_level);

   tcling_callfunc_Wrapper_t      make_wrapper();
   tcling_callfunc_ctor_Wrapper_t make_ctor_wrapper(const TClingClassInfo* info);
   tcling_callfunc_dtor_Wrapper_t make_dtor_wrapper(const TClingClassInfo* info);

   size_t CalculateMinRequiredArguments();

   size_t GetMinRequiredArguments() {
      if (fMinRequiredArguments == (size_t)-1)
         fMinRequiredArguments = CalculateMinRequiredArguments();
      return fMinRequiredArguments;
   }

   // Implemented in source file.
   template <typename T>
   T ExecT(void* address);

public:

   ~TClingCallFunc() = default;

   explicit TClingCallFunc(cling::Interpreter *interp, const CppyyLegacy::TMetaUtils::TNormalizedCtxt &normCtxt)
      : fInterp(interp), fNormCtxt(normCtxt), fWrapper(0)
   {
      fMethod = std::unique_ptr<TClingMethodInfo>(new TClingMethodInfo(interp));
   }

   explicit TClingCallFunc(const TClingMethodInfo &minfo, const CppyyLegacy::TMetaUtils::TNormalizedCtxt &normCtxt)
      : fInterp(minfo.GetInterpreter()), fNormCtxt(normCtxt), fWrapper(0)
   {
      fMethod = std::unique_ptr<TClingMethodInfo>(new TClingMethodInfo(minfo));
   }

   TClingCallFunc(const TClingCallFunc &rhs)
      : fInterp(rhs.fInterp), fNormCtxt(rhs.fNormCtxt), fWrapper(rhs.fWrapper)
   {
      fMethod = std::unique_ptr<TClingMethodInfo>(new TClingMethodInfo(*rhs.fMethod));
   }

   TClingCallFunc &operator=(const TClingCallFunc &rhs) = delete;

   void* ExecDefaultConstructor(const TClingClassInfo* info, void* address = 0,
                                unsigned long nary = 0UL);
   void ExecDestructor(const TClingClassInfo* info, void* address = 0,
                       unsigned long nary = 0UL, bool withFree = true);
   TClingMethodInfo* FactoryMethod() const;
   void Init();
   void Init(const TClingMethodInfo&);
   void Init(std::unique_ptr<TClingMethodInfo>);
   void* InterfaceMethod();
   bool IsValid() const;
   TInterpreter::CallFuncIFacePtr_t IFacePtr();
   const clang::FunctionDecl *GetDecl() {
      if (!fDecl)
         fDecl = fMethod->GetMethodDecl();
      return fDecl;
   }

   int get_wrapper_code(std::string &wrapper_name, std::string &wrapper);

   const clang::FunctionDecl* GetDecl() const {
      if (fDecl)
         return fDecl;
      return fMethod->GetMethodDecl();
   }
   void SetFunc(const TClingClassInfo* info, const char* method,
                const char* arglist, intptr_t* poffset);
   void SetFunc(const TClingClassInfo* info, const char* method,
                const char* arglist, bool objectIsConst, intptr_t* poffset);
   void SetFunc(const TClingMethodInfo* info);
};

} // namespace CppyyLegacy

#endif // ROOT_CallFunc

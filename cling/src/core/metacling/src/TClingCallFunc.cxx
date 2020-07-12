// root/core/meta
// vim: sw=3
// Author: Paul Russo   30/07/2012
// Author: Vassil Vassilev   9/02/2013

/*************************************************************************
 * Copyright (C) 1995-2013, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/** \class TClingCallFunc
Emulation of the CINT CallFunc class.

The CINT C++ interpreter provides an interface for calling
functions through the generated wrappers in dictionaries with
the CallFunc class. This class provides the same functionality,
using an interface as close as possible to CallFunc but the
function metadata and calling service comes from the Cling
C++ interpreter and the Clang C++ compiler, not CINT.
*/

#include "TClingCallFunc.h"

#include "TClingClassInfo.h"
#include "TClingMethodInfo.h"
#include "TInterpreterValue.h"
#include "TClingUtils.h"
#include "TSystem.h"

#include "TError.h"
#include "TCling.h"

#include "cling/Interpreter/CompilationOptions.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/LookupHelper.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/AST.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Lookup.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "clang/Sema/SemaInternal.h"

#include <iomanip>
#include <map>
#include <string>
#include <sstream>


using namespace CppyyLegacy;
using namespace llvm;
using namespace clang;
using namespace std;


namespace CppyyLegacy {

static ULong64_t gWrapperSerial = 0LL;
static const string kIndentString("   ");

static map<const FunctionDecl *, void *> gWrapperStore;
static map<const Decl *, void *> gCtorWrapperStore;
static map<const Decl *, void *> gDtorWrapperStore;

static
inline
void
indent(ostringstream &buf, int indent_level)
{
   for (int i = 0; i < indent_level; ++i) {
      buf << kIndentString;
   }
}

namespace {
   template <typename returnType>
   returnType sv_to(const cling::Value &val)
   {
      QualType QT = val.getType().getCanonicalType();
      if (const BuiltinType *BT =
               dyn_cast<BuiltinType>(&*QT)) {
         //
         //  WARNING!!!
         //
         //  This switch is organized in order-of-declaration
         //  so that the produced assembly code is optimal.
         //  Do not reorder!
         //
         switch (BT->getKind()) {
            case BuiltinType::Void:
               // CINT used to expect a result of 0.
               return (returnType) 0;
               break;
               //
               //  Unsigned Types
               //
            case BuiltinType::Bool:
            case BuiltinType::Char_U: // char on targets where it is unsigned
            case BuiltinType::UChar:
               return (returnType) val.getULL();
               break;

            case BuiltinType::WChar_U:
               // wchar_t on targets where it is unsigned
               // The standard doesn't allow to specify signednedd of wchar_t
               // thus this maps simply to wchar_t.
               return (returnType)(wchar_t) val.getULL();
               break;

            case BuiltinType::Char16:
            case BuiltinType::Char32:
            case BuiltinType::UShort:
            case BuiltinType::UInt:
            case BuiltinType::ULong:
            case BuiltinType::ULongLong:
               return (returnType) val.getULL();
               break;

            case BuiltinType::UInt128:
               // __uint128_t
               break;

               //
               //  Signed Types
               //
            case BuiltinType::Char_S: // char on targets where it is signed
            case BuiltinType::SChar:
               return (returnType) val.getLL();
               break;

            case BuiltinType::WChar_S:
               // wchar_t on targets where it is signed
               // The standard doesn't allow to specify signednedd of wchar_t
               // thus this maps simply to wchar_t.
               return (returnType)(wchar_t) val.getLL();
               break;

            case BuiltinType::Short:
            case BuiltinType::Int:
            case BuiltinType::Long:
            case BuiltinType::LongLong:
               return (returnType) val.getLL();
               break;

            case BuiltinType::Int128:
               break;

            case BuiltinType::Half:
               // half in OpenCL, __fp16 in ARM NEON
               break;

            case BuiltinType::Float:
               return (returnType) val.getFloat();
               break;
            case BuiltinType::Double:
               return (returnType) val.getDouble();
               break;
            case BuiltinType::LongDouble:
               return (returnType) val.getLongDouble();
               break;

            case BuiltinType::NullPtr:
               return (returnType) 0;
               break;

            default:
               break;
         }
      }
      if (QT->isPointerType() || QT->isArrayType() || QT->isRecordType() ||
            QT->isReferenceType()) {
         return (returnType)(intptr_t)val.getPtr();
      }
      if (const EnumType *ET = dyn_cast<EnumType>(&*QT)) {
         if (ET->getDecl()->getIntegerType()->hasSignedIntegerRepresentation())
            return (returnType) val.getLL();
         else
            return (returnType) val.getULL();
      }
      if (QT->isMemberPointerType())
         return (returnType)(intptr_t)val.getPtr();
      ::CppyyLegacy::Error("TClingCallFunc::sv_to", "Invalid Type!");
      QT->dump();
      return 0;
   }

} // unnamed namespace.

size_t TClingCallFunc::CalculateMinRequiredArguments()
{
   // This function is non-const to use caching overload of GetDecl()!
   return GetDecl()->getMinRequiredArguments();
}

void *TClingCallFunc::compile_wrapper(const string &wrapper_name, const string &wrapper,
                                      bool withAccessControl/*=true*/)
{
   return fInterp->compileFunction(wrapper_name, wrapper, false /*ifUnique*/,
                                   false /* withAccessControl */);
}

void TClingCallFunc::collect_type_info(QualType &QT, ostringstream &typedefbuf, std::ostringstream &callbuf,
                                       string &type_name, EReferenceType &refType, bool &isPointer, int indent_level,
                                       bool forArgument)
{
   //
   //  Collect information about type type of a function parameter
   //  needed for building the wrapper function.
   //
   const FunctionDecl *FD = GetDecl();
   PrintingPolicy Policy(FD->getASTContext().getPrintingPolicy());
   refType = kNotReference;
   if (QT->isRecordType() && forArgument) {
      CppyyLegacy::TMetaUtils::GetNormalizedName(type_name, QT, *fInterp, fNormCtxt);
      return;
   }
   if (QT->isFunctionPointerType()) {
      string fp_typedef_name;
      {
         ostringstream nm;
         nm << "FP" << gWrapperSerial++;
         type_name = nm.str();
         raw_string_ostream OS(fp_typedef_name);
         QT.print(OS, Policy, type_name);
         OS.flush();
      }
      for (int i = 0; i < indent_level; ++i) {
         typedefbuf << kIndentString;
      }
      typedefbuf << "typedef " << fp_typedef_name << ";\n";
      return;
   } else if (QT->isMemberPointerType()) {
      string mp_typedef_name;
      {
         ostringstream nm;
         nm << "MP" << gWrapperSerial++;
         type_name = nm.str();
         raw_string_ostream OS(mp_typedef_name);
         QT.print(OS, Policy, type_name);
         OS.flush();
      }
      for (int i = 0; i < indent_level; ++i) {
         typedefbuf << kIndentString;
      }
      typedefbuf << "typedef " << mp_typedef_name << ";\n";
      return;
   } else if (QT->isPointerType()) {
      isPointer = true;
      QT = cast<clang::PointerType>(QT)->getPointeeType();
   } else if (QT->isReferenceType()) {
      if (QT->isRValueReferenceType()) refType = kRValueReference;
      else refType = kLValueReference;
      QT = cast<ReferenceType>(QT)->getPointeeType();
   }
   // Fall through for the array type to deal with reference/pointer ro array type.
   if (QT->isArrayType()) {
      string ar_typedef_name;
      {
         ostringstream ar;
         ar << "AR" << gWrapperSerial++;
         type_name = ar.str();
         raw_string_ostream OS(ar_typedef_name);
         QT.print(OS, Policy, type_name);
         OS.flush();
      }
      for (int i = 0; i < indent_level; ++i) {
         typedefbuf << kIndentString;
      }
      typedefbuf << "typedef " << ar_typedef_name << ";\n";
      return;
   }
   CppyyLegacy::TMetaUtils::GetNormalizedName(type_name, QT, *fInterp, fNormCtxt);
}

void TClingCallFunc::make_narg_ctor(const unsigned N, ostringstream &typedefbuf,
                                    ostringstream &callbuf, const string &class_name,
                                    int indent_level)
{
   // Make a code string that follows this pattern:
   //
   // new ClassName(args...)
   //
   const FunctionDecl *FD = GetDecl();

   callbuf << "new " << class_name << "(";
   for (unsigned i = 0U; i < N; ++i) {
      const ParmVarDecl *PVD = FD->getParamDecl(i);
      QualType Ty = PVD->getType();
      QualType QT = Ty.getCanonicalType();
      // Break if QT is not publicly accessible; that would fail to compile anyway and typically
      // such types would have a default value. Unfortunately this may cause silent problems.
      CXXRecordDecl* rtdecl = QT->getAsCXXRecordDecl();
      if (rtdecl && (rtdecl->getAccess() == AS_private || rtdecl->getAccess() == AS_protected))
         break;
      string type_name;
      EReferenceType refType = kNotReference;
      bool isPointer = false;
      collect_type_info(QT, typedefbuf, callbuf, type_name,
                        refType, isPointer, indent_level, true);
      if (i) {
         callbuf << ',';
         if (i % 2) {
            callbuf << ' ';
         } else {
            callbuf << "\n";
            for (int j = 0; j <= indent_level; ++j) {
               callbuf << kIndentString;
            }
         }
      }
      if (refType != kNotReference) {
         callbuf << "(" << type_name.c_str() <<
                 (refType == kLValueReference ? "&" : "&&") << ")*(" << type_name.c_str() << "*)args["
                 << i << "]";
      } else if (isPointer) {
         callbuf << "*(" << type_name.c_str() << "**)args["
                 << i << "]";
      } else {
         callbuf << "*(" << type_name.c_str() << "*)args[" << i << "]";
      }
   }
   callbuf << ")";
}

static std::map<OverloadedOperatorKind, std::string> binOperatorKinds;
static std::map<OverloadedOperatorKind, std::string> unyOperatorKinds;

void TClingCallFunc::make_narg_call(const std::string &return_type, const unsigned N, ostringstream &typedefbuf,
                                    ostringstream &callbuf, const string &class_name, int indent_level)
{
   //
   // Make a code string that follows this pattern:
   //
   // ((<class>*)obj)-><method>(*(<arg-i-type>*)args[i], ...)
   //
   const FunctionDecl *FD = GetDecl();

   if (binOperatorKinds.empty()) {
      // Prepare lookup map for specific binary operators that are supposed to
      // be left in-place for Cling to resolve.
      binOperatorKinds[OO_Plus]                   = " + ";
      binOperatorKinds[OO_Minus]                  = " - ";
      binOperatorKinds[OO_Star]                   = " * ";
      binOperatorKinds[OO_Slash]                  = " / ";
      binOperatorKinds[OO_Percent]                = " % ";
      binOperatorKinds[OO_Caret]                  = " ^ ";
      binOperatorKinds[OO_Amp]                    = " & ";
      binOperatorKinds[OO_Pipe]                   = " | ";
      binOperatorKinds[OO_Less]                   = " < ";
      binOperatorKinds[OO_Greater]                = " > ";
      binOperatorKinds[OO_LessLess]               = " << ";
      binOperatorKinds[OO_GreaterGreater]         = " >> ";
      binOperatorKinds[OO_LessLessEqual]          = " <<= ";
      binOperatorKinds[OO_GreaterGreaterEqual]    = " >>= ";
      binOperatorKinds[OO_EqualEqual]             = " == ";
      binOperatorKinds[OO_ExclaimEqual]           = " != ";
      binOperatorKinds[OO_LessEqual]              = " <= ";
      binOperatorKinds[OO_GreaterEqual]           = " >= ";
      binOperatorKinds[OO_AmpAmp]                 = " && ";
      binOperatorKinds[OO_PipePipe]               = " || ";
   }

   if (unyOperatorKinds.empty()) {
      unyOperatorKinds[OO_Plus]                   = " + ";
      unyOperatorKinds[OO_Minus]                  = " - ";
      unyOperatorKinds[OO_Star]                   = " * ";
      unyOperatorKinds[OO_Caret]                  = " ^ ";
      unyOperatorKinds[OO_Exclaim]                = " ! ";
      unyOperatorKinds[OO_Amp]                    = " & ";
      unyOperatorKinds[OO_Tilde]                  = " ~ ";
   }

   // Filter out binary operators and replace them simply by that operator
   // to make Cling do the overload resolution. This is mainly for templates,
   // some of which won't compile under Clang5, and others which do not have
   // the proper specialization, even though the argument types match. (It's
   // too late for silent SFINAE at this point.)
   std::string optype = "";
   if ((N == 1 || N == 2) && FD->getDeclName().getNameKind() == DeclarationName::CXXOperatorName) {
       if (N == 2) {
           auto res = binOperatorKinds.find(FD->getDeclName().getCXXOverloadedOperator());
           if (res != binOperatorKinds.end())
               optype = res->second;
       } else {
           auto res = unyOperatorKinds.find(FD->getDeclName().getCXXOverloadedOperator());
           if (res != unyOperatorKinds.end())
               optype = res->second;
       }
   }

   // Sometimes it's necessary that we cast the function we want to call first
   // to its explicit function type before calling it. This is supposed to prevent
   // that we accidentially ending up in a function that is not the one we're
   // supposed to call here (e.g. because the C++ function lookup decides to take
   // another function that better fits).
   // This method has some problems, e.g. when we call a function with default
   // arguments and we don't provide all arguments, we would fail with this pattern.
   // Same applies with member methods which seem to cause parse failures even when
   // we supply the object parameter.
   // Therefore we only use it in cases where we know it works and set this variable
   // to true when we do.
   bool ShouldCastFunction = optype.empty() && \
                             !isa<CXXMethodDecl>(FD) && N == FD->getNumParams() \
                             && !FD->isTemplateInstantiation() && return_type != "(lambda)";
   if (ShouldCastFunction) {
      callbuf << "((" << return_type << " (&)(";
      for (unsigned i = 0U; i < N; ++i) {
         if (i) {
            callbuf << ',';
            if (i % 2) {
               callbuf << ' ';
            } else {
               callbuf << "\n";
               for (int j = 0; j <= indent_level; ++j) {
                  callbuf << kIndentString;
               }
            }
         }
         const ParmVarDecl *PVD = FD->getParamDecl(i);
         QualType Ty = PVD->getType();
         QualType QT = Ty.getCanonicalType();
         std::string arg_type;
         CppyyLegacy::TMetaUtils::GetNormalizedName(arg_type, QT, *fInterp, fNormCtxt);
         callbuf << arg_type;
      }
      if (FD->isVariadic())
         callbuf << ", ...";
      callbuf << "))";
   }

   std::string function_name;
   {
      llvm::raw_string_ostream stream(function_name);
      FD->getNameForDiagnostic(stream, FD->getASTContext().getPrintingPolicy(), /*Qualified=*/false);
   }
#ifdef _WIN32
// TODO: This is not a true solution, but make_unique is so far the only relevant
// case: on Windows, make_unique is a variadic template and Cling finds the expanded
// version which can not be used in-place, so drop the expansion here. To be fixed
// with an update of Cling's lookup helper for templated functions.
   if (function_name.compare(0, 12, "make_unique<") == 0)
       function_name = function_name.substr(0, function_name.find(','))+'>';
#endif

   if (optype.empty() || N == 1) {
      if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
         // This is a class, struct, or union member.
         if (MD->isConst())
            callbuf << "((const " << class_name << "*)obj)->";
         else
            callbuf << "((" << class_name << "*)obj)->";
      } else if (const NamedDecl *ND =
                    dyn_cast<NamedDecl>(FD->getDeclContext())) {
         // This is a namespace member.
         (void) ND;
         callbuf << class_name << "::";
      }
      //   callbuf << fMethod->Name() << "(";
      {
         callbuf << (optype.empty() ? function_name : "operator"+optype);
      }
   }
   if (ShouldCastFunction) callbuf << ")";

   callbuf << "(";
   for (unsigned i = 0U; i < N; ++i) {
      const ParmVarDecl *PVD = FD->getParamDecl(i);
      QualType Ty = PVD->getType();
      QualType QT = Ty.getCanonicalType();
      // Break if QT is not publicly accessible; that would fail to compile anyway and typically
      // such types would have a default value. Unfortunately this may cause silent problems.
      CXXRecordDecl* rtdecl = QT->getAsCXXRecordDecl();
      if (rtdecl && (rtdecl->getAccess() == AS_private || rtdecl->getAccess() == AS_protected))
         break;
      string type_name;
      EReferenceType refType = kNotReference;
      bool isPointer = false;
      collect_type_info(QT, typedefbuf, callbuf, type_name, refType, isPointer, indent_level, true);

      if (i) {
         if (optype.empty()) {
            callbuf << ',';
            if (i % 2) {
               callbuf << ' ';
            } else {
               callbuf << "\n";
               for (int j = 0; j <= indent_level; ++j) {
                  callbuf << kIndentString;
               }
            }
         } else {
            callbuf << optype;
         }
      }

      if (refType != kNotReference) {
         callbuf << "(" << type_name.c_str() <<
                 (refType == kLValueReference ? "&" : "&&") << ")*(" << type_name.c_str() << "*)args["
                 << i << "]";
      } else if (isPointer) {
         callbuf << "*(" << type_name.c_str() << "**)args["
                 << i << "]";
      } else {
         // By-value construction; this may either copy or move, but there is no
         // information here in terms of intent. Thus, simply assume that the intent
         // is to move if there is no viable copy constructor (ie. if the code would
         // otherwise fail to even compile).

         // There does not appear to be a simple way of determining whether a viable
         // copy constructor exists, so check for the most common case: the trivial
         // one, but not uniquely available, while there is a move constructor.
         if (rtdecl && (rtdecl->hasTrivialCopyConstructor() && !rtdecl->hasSimpleCopyConstructor()) \
               && rtdecl->hasMoveConstructor()) {
            // move construction as needed for classes (note that this is implicit)
            callbuf << "std::move(*(" << type_name.c_str() << "*)args[" << i << "])";
         } else {
            // otherwise, and for builtins, use copy construction of temporary*/
            callbuf << "*(" << type_name.c_str() << "*)args[" << i << "]";
         }
      }
   }
   callbuf << ")";
}

void TClingCallFunc::make_narg_ctor_with_return(const unsigned N, const string &class_name,
      ostringstream &buf, int indent_level)
{
   // Make a code string that follows this pattern:
   //
   // if (ret) {
   //    (*(ClassName**)ret) = new ClassName(args...);
   // }
   // else {
   //    new ClassName(args...);
   // }
   //
   for (int i = 0; i < indent_level; ++i) {
      buf << kIndentString;
   }
   buf << "if (ret) {\n";
   ++indent_level;
   {
      ostringstream typedefbuf;
      ostringstream callbuf;
      //
      //  Write the return value assignment part.
      //
      for (int i = 0; i < indent_level; ++i) {
         callbuf << kIndentString;
      }
      callbuf << "(*(" << class_name << "**)ret) = ";
      //
      //  Write the actual new expression.
      //
      make_narg_ctor(N, typedefbuf, callbuf, class_name, indent_level);
      //
      //  End the new expression statement.
      //
      callbuf << ";\n";
      for (int i = 0; i < indent_level; ++i) {
         callbuf << kIndentString;
      }
      callbuf << "return;\n";
      //
      //  Output the whole new expression and return statement.
      //
      buf << typedefbuf.str() << callbuf.str();
   }
   --indent_level;
   for (int i = 0; i < indent_level; ++i) {
      buf << kIndentString;
   }
   buf << "}\n";
   for (int i = 0; i < indent_level; ++i) {
      buf << kIndentString;
   }
   buf << "else {\n";
   ++indent_level;
   {
      ostringstream typedefbuf;
      ostringstream callbuf;
      for (int i = 0; i < indent_level; ++i) {
         callbuf << kIndentString;
      }
      make_narg_ctor(N, typedefbuf, callbuf, class_name, indent_level);
      callbuf << ";\n";
      for (int i = 0; i < indent_level; ++i) {
         callbuf << kIndentString;
      }
      callbuf << "return;\n";
      buf << typedefbuf.str() << callbuf.str();
   }
   --indent_level;
   for (int i = 0; i < indent_level; ++i) {
      buf << kIndentString;
   }
   buf << "}\n";
}

int TClingCallFunc::get_wrapper_code(std::string &wrapper_name, std::string &wrapper)
{
   const FunctionDecl *FD = GetDecl();
   assert(FD && "generate_wrapper called without a function decl!");
   ASTContext &Context = FD->getASTContext();
   PrintingPolicy Policy(Context.getPrintingPolicy());
   //
   //  Get the class or namespace name.
   //
   string class_name;
   if (const TypeDecl *TD = dyn_cast<TypeDecl>(FD->getDeclContext())) {
      // This is a class, struct, or union member.
      QualType QT(TD->getTypeForDecl(), 0);
      CppyyLegacy::TMetaUtils::GetNormalizedName(class_name, QT, *fInterp, fNormCtxt);
   } else if (const NamedDecl *ND = dyn_cast<NamedDecl>(FD->getDeclContext())) {
      // This is a namespace member.
      raw_string_ostream stream(class_name);
      ND->getNameForDiagnostic(stream, Policy, /*Qualified=*/true);
      stream.flush();
   }
   //
   //  Check to make sure that we can
   //  instantiate and codegen this function.
   //
   bool needInstantiation = false;
   const FunctionDecl *Definition = 0;
   if (!FD->isDefined(Definition)) {
      FunctionDecl::TemplatedKind TK = FD->getTemplatedKind();
      switch (TK) {
      case FunctionDecl::TK_NonTemplate: {
         // Ordinary function, not a template specialization.
         // Note: This might be ok, the body might be defined
         //       in a library, and all we have seen is the
         //       header file.
         //::CppyyLegacy::Error("TClingCallFunc::make_wrapper",
         //      "Cannot make wrapper for a function which is "
         //      "declared but not defined!");
         // return 0;
      } break;
      case FunctionDecl::TK_FunctionTemplate: {
         // This decl is actually a function template,
         // not a function at all.
         ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a function template!");
         return 0;
      } break;
      case FunctionDecl::TK_MemberSpecialization: {
         // This function is the result of instantiating an ordinary
         // member function of a class template, or of instantiating
         // an ordinary member function of a class member of a class
         // template, or of specializing a member function template
         // of a class template, or of specializing a member function
         // template of a class member of a class template.
         if (!FD->isTemplateInstantiation()) {
            // We are either TSK_Undeclared or
            // TSK_ExplicitSpecialization.
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            //::CppyyLegacy::Error("TClingCallFunc::make_wrapper",
            //      "Cannot make wrapper for a function template "
            //      "explicit specialization which is declared "
            //      "but not defined!");
            // return 0;
            break;
         }
         const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern();
         if (!Pattern) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a member function "
                                                    "instantiation with no pattern!");
            return 0;
         }
         FunctionDecl::TemplatedKind PTK = Pattern->getTemplatedKind();
         TemplateSpecializationKind PTSK = Pattern->getTemplateSpecializationKind();
         if (
            // The pattern is an ordinary member function.
            (PTK == FunctionDecl::TK_NonTemplate) ||
            // The pattern is an explicit specialization, and
            // so is not a template.
            ((PTK != FunctionDecl::TK_FunctionTemplate) &&
             ((PTSK == TSK_Undeclared) || (PTSK == TSK_ExplicitSpecialization))) ||
            // This is an extern declaration of a promised explicit instantation only
            (FD->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDeclaration)) {
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            break;
         } else if (!Pattern->hasBody()) {
            //::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a member function "
            //                                        "instantiation with no body!");
            needInstantiation = false;
         } else if (FD->isImplicitlyInstantiable()) {
            needInstantiation = true;
         }
      } break;
      case FunctionDecl::TK_FunctionTemplateSpecialization: {
         // This function is the result of instantiating a function
         // template or possibly an explicit specialization of a
         // function template.  Could be a namespace scope function or a
         // member function.
         if (!FD->isTemplateInstantiation()) {
            // We are either TSK_Undeclared or
            // TSK_ExplicitSpecialization.
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            //::CppyyLegacy::Error("TClingCallFunc::make_wrapper",
            //      "Cannot make wrapper for a function template "
            //      "explicit specialization which is declared "
            //      "but not defined!");
            // return 0;
            break;
         }
         const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern();
         if (!Pattern) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a function template"
                                                    "instantiation with no pattern!");
            return 0;
         }
         FunctionDecl::TemplatedKind PTK = Pattern->getTemplatedKind();
         TemplateSpecializationKind PTSK = Pattern->getTemplateSpecializationKind();
         if (
            // The pattern is an ordinary member function.
            (PTK == FunctionDecl::TK_NonTemplate) ||
            // The pattern is an explicit specialization, and
            // so is not a template.
            ((PTK != FunctionDecl::TK_FunctionTemplate) &&
             ((PTSK == TSK_Undeclared) || (PTSK == TSK_ExplicitSpecialization))) ||
            // This is an extern declaration of a promised explicit instantation only
            (FD->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDeclaration)) {
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            break;
         }
         if (!Pattern->hasBody()) {
         //   ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a function template"
         //                                           "instantiation with no body!");
            needInstantiation = false;
         } else if (FD->isImplicitlyInstantiable()) {
            needInstantiation = true;
         }
      } break;
      case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
         // This function is the result of instantiating or
         // specializing a  member function of a class template,
         // or a member function of a class member of a class template,
         // or a member function template of a class template, or a
         // member function template of a class member of a class
         // template where at least some part of the function is
         // dependent on a template argument.
         if (!FD->isTemplateInstantiation()) {
            // We are either TSK_Undeclared or
            // TSK_ExplicitSpecialization.
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            //::CppyyLegacy::Error("TClingCallFunc::make_wrapper",
            //      "Cannot make wrapper for a dependent function "
            //      "template explicit specialization which is declared "
            //      "but not defined!");
            // return 0;
            break;
         }
         const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern();
         if (!Pattern) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a dependent function template"
                                                    "instantiation with no pattern!");
            return 0;
         }
         FunctionDecl::TemplatedKind PTK = Pattern->getTemplatedKind();
         TemplateSpecializationKind PTSK = Pattern->getTemplateSpecializationKind();
         if (
            // The pattern is an ordinary member function.
            (PTK == FunctionDecl::TK_NonTemplate) ||
            // The pattern is an explicit specialization, and
            // so is not a template.
            ((PTK != FunctionDecl::TK_FunctionTemplate) &&
             ((PTSK == TSK_Undeclared) || (PTSK == TSK_ExplicitSpecialization))) ||
            // This is an extern declaration of a promised explicit instantation only
            (FD->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDeclaration)) {
            // Note: This might be ok, the body might be defined
            //       in a library, and all we have seen is the
            //       header file.
            break;
         }
         if (!Pattern->hasBody()) {
            //::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a dependent function template"
            //                                        "instantiation with no body!");
            needInstantiation = false;
         } else if (FD->isImplicitlyInstantiable()) {
            needInstantiation = true;
         }
      } break;
      default: {
         // Will only happen if clang implementation changes.
         // Protect ourselves in case that happens.
         ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Unhandled template kind!");
         return 0;
      } break;
      }
      // We do not set needInstantiation to true in these cases:
      //
      // isInvalidDecl()
      // TSK_Undeclared
      // TSK_ExplicitInstantiationDefinition
      // TSK_ExplicitSpecialization && !getClassScopeSpecializationPattern()
      // TSK_ExplicitInstantiationDeclaration &&
      //    getTemplateInstantiationPattern() &&
      //    PatternDecl->hasBody() &&
      //    !PatternDecl->isInlined()
      //
      // Set it true in these cases:
      //
      // TSK_ImplicitInstantiation
      // TSK_ExplicitInstantiationDeclaration && (!getPatternDecl() ||
      //    !PatternDecl->hasBody() || PatternDecl->isInlined())
      //
   }
   if (needInstantiation && \
         FD->getTemplateSpecializationKind() != TSK_ExplicitInstantiationDeclaration) {
      clang::FunctionDecl *FDmod = const_cast<clang::FunctionDecl *>(FD);
      clang::Sema &S = fInterp->getSema();
      // Could trigger deserialization of decls.
      cling::Interpreter::PushTransactionRAII RAII(fInterp);
      S.InstantiateFunctionDefinition(SourceLocation(), FDmod,
                                      /*Recursive=*/true,
                                      /*DefinitionRequired=*/true);
      if (!FD->isDefined(Definition)) {
         ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Failed to force template instantiation!");
         return 0;
      }
   }
   if (Definition) {
      FunctionDecl::TemplatedKind TK = Definition->getTemplatedKind();
      switch (TK) {
      case FunctionDecl::TK_NonTemplate: {
         // Ordinary function, not a template specialization.
         if (Definition->isDeleted()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a deleted function!");
            return 0;
         } else if (Definition->isLateTemplateParsed()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a late template parsed "
                                                    "function!");
            return 0;
         }
         // else if (Definition->isDefaulted()) {
         //   // Might not have a body, but we can still use it.
         //}
         // else {
         //   // Has a body.
         //}
      } break;
      case FunctionDecl::TK_FunctionTemplate: {
         // This decl is actually a function template,
         // not a function at all.
         ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a function template!");
         return 0;
      } break;
      case FunctionDecl::TK_MemberSpecialization: {
         // This function is the result of instantiating an ordinary
         // member function of a class template or of a member class
         // of a class template.
         if (Definition->isDeleted()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a deleted member function "
                                                    "of a specialization!");
            return 0;
         } else if (Definition->isLateTemplateParsed()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a late template parsed "
                                                    "member function of a specialization!");
            return 0;
         }
         // else if (Definition->isDefaulted()) {
         //   // Might not have a body, but we can still use it.
         //}
         // else {
         //   // Has a body.
         //}
      } break;
      case FunctionDecl::TK_FunctionTemplateSpecialization: {
         // This function is the result of instantiating a function
         // template or possibly an explicit specialization of a
         // function template.  Could be a namespace scope function or a
         // member function.
         if (Definition->isDeleted()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a deleted function "
                                                    "template specialization!");
            return 0;
         } else if (Definition->isLateTemplateParsed()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a late template parsed "
                                                    "function template specialization!");
            return 0;
         }
         // else if (Definition->isDefaulted()) {
         //   // Might not have a body, but we can still use it.
         //}
         // else {
         //   // Has a body.
         //}
      } break;
      case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
         // This function is the result of instantiating or
         // specializing a  member function of a class template,
         // or a member function of a class member of a class template,
         // or a member function template of a class template, or a
         // member function template of a class member of a class
         // template where at least some part of the function is
         // dependent on a template argument.
         if (Definition->isDeleted()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a deleted dependent function "
                                                    "template specialization!");
            return 0;
         } else if (Definition->isLateTemplateParsed()) {
            ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Cannot make wrapper for a late template parsed "
                                                    "dependent function template specialization!");
            return 0;
         }
         // else if (Definition->isDefaulted()) {
         //   // Might not have a body, but we can still use it.
         //}
         // else {
         //   // Has a body.
         //}
      } break;
      default: {
         // Will only happen if clang implementation changes.
         // Protect ourselves in case that happens.
         ::CppyyLegacy::Error("TClingCallFunc::make_wrapper", "Unhandled template kind!");
         return 0;
      } break;
      }
   }
   unsigned min_args = GetMinRequiredArguments();
   unsigned num_params = FD->getNumParams();
   //
   //  Make the wrapper name.
   //
   {
      ostringstream buf;
      buf << "__cf";
      // const NamedDecl* ND = dyn_cast<NamedDecl>(FD);
      // string mn;
      // fInterp->maybeMangleDeclName(ND, mn);
      // buf << '_' << mn;
      buf << '_' << gWrapperSerial++;
      wrapper_name = buf.str();
   }
   //
   //  Write the wrapper code.
   // FIXME: this should be synthesized into the AST!
   //
   int indent_level = 0;
   ostringstream buf;
   buf << "#pragma clang diagnostic push\n"
          "#pragma clang diagnostic ignored \"-Wformat-security\"\n"
          "__attribute__((used)) "
          "extern \"C\" void ";
   buf << wrapper_name;
   buf << "(void* obj, int nargs, void** args, void* ret)\n"
          "{\n";
   ++indent_level;
   if (min_args == num_params) {
      // No parameters with defaults.
      make_narg_call_with_return(num_params, class_name, buf, indent_level);
   } else {
      // We need one function call clause compiled for every
      // possible number of arguments per call.
      for (unsigned N = min_args; N <= num_params; ++N) {
         for (int i = 0; i < indent_level; ++i) {
            buf << kIndentString;
         }
         buf << "if (nargs == " << N << ") {\n";
         ++indent_level;
         make_narg_call_with_return(N, class_name, buf, indent_level);
         --indent_level;
         for (int i = 0; i < indent_level; ++i) {
            buf << kIndentString;
         }
         buf << "}\n";
      }
   }
   --indent_level;
   buf << "}\n"
          "#pragma clang diagnostic pop";
   wrapper = buf.str();
   return 1;
}

void TClingCallFunc::make_narg_call_with_return(const unsigned N, const string &class_name,
      ostringstream &buf, int indent_level)
{
   // Make a code string that follows this pattern:
   //
   // if (ret) {
   //    new (ret) (return_type) ((class_name*)obj)->func(args...);
   // }
   // else {
   //    ((class_name*)obj)->func(args...);
   // }
   //
   const FunctionDecl *FD = GetDecl();
   if (const CXXConstructorDecl *CD = dyn_cast<CXXConstructorDecl>(FD)) {
      (void) CD;
      make_narg_ctor_with_return(N, class_name, buf, indent_level);
      return;
   }
   QualType QT = FD->getReturnType();
   CXXRecordDecl* rtdecl = QT->getAsCXXRecordDecl();
   if (!rtdecl || (rtdecl->getAccess() != AS_private && rtdecl->getAccess() != AS_protected))
       QT = QT.getCanonicalType();

   if (QT->isVoidType()) {
      ostringstream typedefbuf;
      ostringstream callbuf;
      for (int i = 0; i < indent_level; ++i) {
         callbuf << kIndentString;
      }
      make_narg_call("void", N, typedefbuf, callbuf, class_name, indent_level);
      callbuf << ";\n";
      for (int i = 0; i < indent_level; ++i) {
         callbuf << kIndentString;
      }
      callbuf << "return;\n";
      buf << typedefbuf.str() << callbuf.str();
   } else {
      for (int i = 0; i < indent_level; ++i) {
         buf << kIndentString;
      }

      string type_name;
      EReferenceType refType = kNotReference;
      bool isPointer = false;

      buf << "if (ret) {\n";
      ++indent_level;
      {
         ostringstream typedefbuf;
         ostringstream callbuf;
         //
         //  Write the placement part of the placement new.
         //
         for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
         }
         collect_type_info(QT, typedefbuf, callbuf, type_name,
                           refType, isPointer, indent_level, false);

         bool IsNotLambda = type_name != "(lambda)";
         if (IsNotLambda) {
            callbuf << "new (ret) ";
            //
            //  Write the type part of the placement new.
            //
            callbuf << "(" << gCling->ReduceType(type_name);
            if (refType != kNotReference) {
               callbuf << "*) (&";
               type_name += "&";
            } else if (isPointer) {
               callbuf << "*) (";
               type_name += "*";
            } else {
               callbuf << ") (";
            }
         } else {
            // no cast for lambda's (return type wrapped later)
            callbuf << "auto lll = (";
         }
         //
         //  Write the actual function call.
         //
         make_narg_call(type_name, N, typedefbuf, callbuf, class_name, indent_level);
         //
         //  End the placement new.
         //
         callbuf << ");\n";
         if (!IsNotLambda) {
            for (int i = 0; i < indent_level; ++i) {
               callbuf << kIndentString;
            }
            callbuf << "new (ret) __cling_internal::FT<decltype(lll)>::F{lll};\n";
         }
         for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
         }
         callbuf << "return;\n";
         //
         //  Output the whole placement new expression and return statement.
         //
         buf << typedefbuf.str() << callbuf.str();
      }
      --indent_level;
      for (int i = 0; i < indent_level; ++i) {
         buf << kIndentString;
      }
      buf << "}\n";
      for (int i = 0; i < indent_level; ++i) {
         buf << kIndentString;
      }
      buf << "else {\n";
      ++indent_level;
      {
         ostringstream typedefbuf;
         ostringstream callbuf;
         for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
         }
         make_narg_call(type_name, N, typedefbuf, callbuf, class_name, indent_level);
         callbuf << ";\n";
         for (int i = 0; i < indent_level; ++i) {
            callbuf << kIndentString;
         }
         callbuf << "return;\n";
         buf << typedefbuf.str() << callbuf.str();
      }
      --indent_level;
      for (int i = 0; i < indent_level; ++i) {
         buf << kIndentString;
      }
      buf << "}\n";
   }
}

tcling_callfunc_Wrapper_t TClingCallFunc::make_wrapper()
{
   R__LOCKGUARD_CLING(gInterpreterMutex);

   const FunctionDecl *FD = GetDecl();
   string wrapper_name;
   string wrapper;

   if (get_wrapper_code(wrapper_name, wrapper) == 0) return 0;

   //fprintf(stderr, "%s\n", wrapper.c_str());
   //
   //  Compile the wrapper code.
   //
   void *F = compile_wrapper(wrapper_name, wrapper);
   if (F) {
      gWrapperStore.insert(make_pair(FD, F));
   } else {
      ::CppyyLegacy::Error("TClingCallFunc::make_wrapper",
            "Failed to compile\n  ==== SOURCE BEGIN ====\n%s\n  ==== SOURCE END ====",
            wrapper.c_str());
   }
   return (tcling_callfunc_Wrapper_t)F;
}

tcling_callfunc_ctor_Wrapper_t TClingCallFunc::make_ctor_wrapper(const TClingClassInfo *info)
{
   // Make a code string that follows this pattern:
   //
   // void
   // unique_wrapper_ddd(void** ret, void* arena, unsigned long nary)
   // {
   //    if (!arena) {
   //       if (!nary) {
   //          *ret = new ClassName;
   //       }
   //       else {
   //          *ret = new ClassName[nary];
   //       }
   //    }
   //    else {
   //       if (!nary) {
   //          *ret = new (arena) ClassName;
   //       }
   //       else {
   //          *ret = new (arena) ClassName[nary];
   //       }
   //    }
   // }
   //
   // Note:
   //
   // If the class is of POD type, the form:
   //
   //    new ClassName;
   //
   // does not initialize the object at all, and the form:
   //
   //    new ClassName();
   //
   // default-initializes the object.
   //
   // We are using the form without parentheses because that is what
   // CINT did.
   //
   //--
   ASTContext &Context = info->GetDecl()->getASTContext();
   PrintingPolicy Policy(Context.getPrintingPolicy());
   Policy.SuppressTagKeyword = true;
   Policy.SuppressUnwrittenScope = true;
   //
   //  Get the class or namespace name.
   //
   string class_name;
   if (const TypeDecl *TD = dyn_cast<TypeDecl>(info->GetDecl())) {
      // This is a class, struct, or union member.
      QualType QT(TD->getTypeForDecl(), 0);
      CppyyLegacy::TMetaUtils::GetNormalizedName(class_name, QT, *fInterp, fNormCtxt);
   } else if (const NamedDecl *ND = dyn_cast<NamedDecl>(info->GetDecl())) {
      // This is a namespace member.
      raw_string_ostream stream(class_name);
      ND->getNameForDiagnostic(stream, Policy, /*Qualified=*/true);
      stream.flush();
   }
   //
   //  Make the wrapper name.
   //
   string wrapper_name;
   {
      ostringstream buf;
      buf << "__ctor";
      //const NamedDecl* ND = dyn_cast<NamedDecl>(FD);
      //string mn;
      //fInterp->maybeMangleDeclName(ND, mn);
      //buf << '_dtor_' << mn;
      buf << '_' << gWrapperSerial++;
      wrapper_name = buf.str();
   }
   //
   //  Write the wrapper code.
   //
   int indent_level = 0;
   ostringstream buf;
   buf << "__attribute__((used)) ";
   buf << "extern \"C\" void ";
   buf << wrapper_name;
   buf << "(void** ret, void* arena, unsigned long nary)\n";
   buf << "{\n";
   //    if (!arena) {
   //       if (!nary) {
   //          *ret = new ClassName;
   //       }
   //       else {
   //          *ret = new ClassName[nary];
   //       }
   //    }
   ++indent_level;
   indent(buf, indent_level);
   buf << "if (!arena) {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "if (!nary) {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "*ret = new " << class_name << ";\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   indent(buf, indent_level);
   buf << "else {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "*ret = new " << class_name << "[nary];\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   //    else {
   //       if (!nary) {
   //          *ret = new (arena) ClassName;
   //       }
   //       else {
   //          *ret = new (arena) ClassName[nary];
   //       }
   //    }
   indent(buf, indent_level);
   buf << "else {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "if (!nary) {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "*ret = new (arena) " << class_name << ";\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   indent(buf, indent_level);
   buf << "else {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "*ret = new (arena) " << class_name << "[nary];\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   // End wrapper.
   --indent_level;
   buf << "}\n";
   // Done.
   string wrapper(buf.str());
   //fprintf(stderr, "%s\n", wrapper.c_str());
   //
   //  Compile the wrapper code.
   //
   void *F = compile_wrapper(wrapper_name, wrapper,
                             /*withAccessControl=*/false);
   if (F) {
      gCtorWrapperStore.insert(make_pair(info->GetDecl(), F));
   } else {
      ::CppyyLegacy::Error("TClingCallFunc::make_ctor_wrapper",
            "Failed to compile\n  ==== SOURCE BEGIN ====\n%s\n  ==== SOURCE END ====",
            wrapper.c_str());
   }
   return (tcling_callfunc_ctor_Wrapper_t)F;
}

tcling_callfunc_dtor_Wrapper_t
TClingCallFunc::make_dtor_wrapper(const TClingClassInfo *info)
{
   // Make a code string that follows this pattern:
   //
   // void
   // unique_wrapper_ddd(void* obj, unsigned long nary, int withFree)
   // {
   //    if (withFree) {
   //       if (!nary) {
   //          delete (ClassName*) obj;
   //       }
   //       else {
   //          delete[] (ClassName*) obj;
   //       }
   //    }
   //    else {
   //       typedef ClassName DtorName;
   //       if (!nary) {
   //          ((ClassName*)obj)->~DtorName();
   //       }
   //       else {
   //          for (unsigned long i = nary - 1; i > -1; --i) {
   //             (((ClassName*)obj)+i)->~DtorName();
   //          }
   //       }
   //    }
   // }
   //
   //--
   ASTContext &Context = info->GetDecl()->getASTContext();
   PrintingPolicy Policy(Context.getPrintingPolicy());
   Policy.SuppressTagKeyword = true;
   Policy.SuppressUnwrittenScope = true;
   //
   //  Get the class or namespace name.
   //
   string class_name;
   if (const TypeDecl *TD = dyn_cast<TypeDecl>(info->GetDecl())) {
      // This is a class, struct, or union member.
      QualType QT(TD->getTypeForDecl(), 0);
      CppyyLegacy::TMetaUtils::GetNormalizedName(class_name, QT, *fInterp, fNormCtxt);
   } else if (const NamedDecl *ND = dyn_cast<NamedDecl>(info->GetDecl())) {
      // This is a namespace member.
      raw_string_ostream stream(class_name);
      ND->getNameForDiagnostic(stream, Policy, /*Qualified=*/true);
      stream.flush();
   }
   //
   //  Make the wrapper name.
   //
   string wrapper_name;
   {
      ostringstream buf;
      buf << "__dtor";
      //const NamedDecl* ND = dyn_cast<NamedDecl>(FD);
      //string mn;
      //fInterp->maybeMangleDeclName(ND, mn);
      //buf << '_dtor_' << mn;
      buf << '_' << gWrapperSerial++;
      wrapper_name = buf.str();
   }
   //
   //  Write the wrapper code.
   //
   int indent_level = 0;
   ostringstream buf;
   buf << "__attribute__((used)) ";
   buf << "extern \"C\" void ";
   buf << wrapper_name;
   buf << "(void* obj, unsigned long nary, int withFree)\n";
   buf << "{\n";
   //    if (withFree) {
   //       if (!nary) {
   //          delete (ClassName*) obj;
   //       }
   //       else {
   //          delete[] (ClassName*) obj;
   //       }
   //    }
   ++indent_level;
   indent(buf, indent_level);
   buf << "if (withFree) {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "if (!nary) {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "delete (" << class_name << "*) obj;\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   indent(buf, indent_level);
   buf << "else {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "delete[] (" << class_name << "*) obj;\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   //    else {
   //       typedef ClassName Nm;
   //       if (!nary) {
   //          ((Nm*)obj)->~Nm();
   //       }
   //       else {
   //          for (unsigned long i = nary - 1; i > -1; --i) {
   //             (((Nm*)obj)+i)->~Nm();
   //          }
   //       }
   //    }
   indent(buf, indent_level);
   buf << "else {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "typedef " << class_name << " Nm;\n";
   buf << "if (!nary) {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "((Nm*)obj)->~Nm();\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   indent(buf, indent_level);
   buf << "else {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "do {\n";
   ++indent_level;
   indent(buf, indent_level);
   buf << "(((Nm*)obj)+(--nary))->~Nm();\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "} while (nary);\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   --indent_level;
   indent(buf, indent_level);
   buf << "}\n";
   // End wrapper.
   --indent_level;
   buf << "}\n";
   // Done.
   string wrapper(buf.str());
   //fprintf(stderr, "%s\n", wrapper.c_str());
   //
   //  Compile the wrapper code.
   //
   void *F = compile_wrapper(wrapper_name, wrapper,
                             /*withAccessControl=*/false);
   if (F) {
      gDtorWrapperStore.insert(make_pair(info->GetDecl(), F));
   } else {
      ::CppyyLegacy::Error("TClingCallFunc::make_dtor_wrapper",
            "Failed to compile\n  ==== SOURCE BEGIN ====\n%s\n  ==== SOURCE END ====",
            wrapper.c_str());
   }

   return (tcling_callfunc_dtor_Wrapper_t)F;
}

void *TClingCallFunc::ExecDefaultConstructor(const TClingClassInfo *info, void *address /*=0*/,
      unsigned long nary /*= 0UL*/)
{
   if (!info->IsValid()) {
      ::CppyyLegacy::Error("TClingCallFunc::ExecDefaultConstructor", "Invalid class info!");
      return 0;
   }
   tcling_callfunc_ctor_Wrapper_t wrapper = 0;
   {
      R__LOCKGUARD_CLING(gInterpreterMutex);
      const Decl *D = info->GetDecl();
      map<const Decl *, void *>::iterator I = gCtorWrapperStore.find(D);
      if (I != gCtorWrapperStore.end()) {
         wrapper = (tcling_callfunc_ctor_Wrapper_t) I->second;
      } else {
         wrapper = make_ctor_wrapper(info);
      }
   }
   if (!wrapper) {
      ::CppyyLegacy::Error("TClingCallFunc::ExecDefaultConstructor",
            "Called with no wrapper, not implemented!");
      return 0;
   }
   void *obj = 0;
   (*wrapper)(&obj, address, nary);
   return obj;
}

void TClingCallFunc::ExecDestructor(const TClingClassInfo *info, void *address /*=0*/,
                                    unsigned long nary /*= 0UL*/, bool withFree /*= true*/)
{
   if (!info->IsValid()) {
      ::CppyyLegacy::Error("TClingCallFunc::ExecDestructor", "Invalid class info!");
      return;
   }

   tcling_callfunc_dtor_Wrapper_t wrapper = 0;
   {
      R__LOCKGUARD_CLING(gInterpreterMutex);
      const Decl *D = info->GetDecl();
      map<const Decl *, void *>::iterator I = gDtorWrapperStore.find(D);
      if (I != gDtorWrapperStore.end()) {
         wrapper = (tcling_callfunc_dtor_Wrapper_t) I->second;
      } else {
         wrapper = make_dtor_wrapper(info);
      }
   }
   if (!wrapper) {
      ::CppyyLegacy::Error("TClingCallFunc::ExecDestructor",
            "Called with no wrapper, not implemented!");
      return;
   }
   (*wrapper)(address, nary, withFree);
}

TClingMethodInfo *
TClingCallFunc::FactoryMethod() const
{
   return new TClingMethodInfo(*fMethod);
}

void TClingCallFunc::Init()
{
   fMethod.reset();
   fWrapper = 0;
   fDecl = nullptr;
   fMinRequiredArguments = -1;
}

void TClingCallFunc::Init(const TClingMethodInfo &minfo)
{
   Init();
   fMethod = std::unique_ptr<TClingMethodInfo>(new TClingMethodInfo(minfo));
}

void TClingCallFunc::Init(std::unique_ptr<TClingMethodInfo> minfo)
{
   Init();
   fMethod = std::move(minfo);
}

void *TClingCallFunc::InterfaceMethod()
{
   if (!IsValid()) {
      return 0;
   }
   if (!fWrapper) {
      const FunctionDecl *decl = GetDecl();

      R__LOCKGUARD_CLING(gInterpreterMutex);
      map<const FunctionDecl *, void *>::iterator I = gWrapperStore.find(decl);
      if (I != gWrapperStore.end()) {
         fWrapper = (tcling_callfunc_Wrapper_t) I->second;
      } else {
         fWrapper = make_wrapper();
      }
   }
   return (void *)fWrapper;
}

bool TClingCallFunc::IsValid() const
{
   if (!fMethod) {
      return false;
   }
   return fMethod->IsValid();
}

TInterpreter::CallFuncIFacePtr_t TClingCallFunc::IFacePtr()
{
   if (!IsValid()) {
      ::CppyyLegacy::Error("TClingCallFunc::IFacePtr(kind)",
            "Attempt to get interface while invalid.");
      return TInterpreter::CallFuncIFacePtr_t();
   }
   if (!fWrapper) {
      const FunctionDecl *decl = GetDecl();

      R__LOCKGUARD_CLING(gInterpreterMutex);
      map<const FunctionDecl *, void *>::iterator I = gWrapperStore.find(decl);
      if (I != gWrapperStore.end()) {
         fWrapper = (tcling_callfunc_Wrapper_t) I->second;
      } else {
         fWrapper = make_wrapper();
      }
   }
   return TInterpreter::CallFuncIFacePtr_t(fWrapper);
}

void TClingCallFunc::SetFunc(const TClingClassInfo *info, const char *method, const char *arglist,
                             intptr_t *poffset)
{
   SetFunc(info, method, arglist, false, poffset);
}

void TClingCallFunc::SetFunc(const TClingClassInfo *info, const char *method, const char *arglist,
                             bool objectIsConst, intptr_t *poffset)
{
   Init(std::unique_ptr<TClingMethodInfo>(new TClingMethodInfo(fInterp)));
   if (poffset) {
      *poffset = 0L;
   }
   if (!info->IsValid()) {
      ::CppyyLegacy::Error("TClingCallFunc::SetFunc", "Class info is invalid!");
      return;
   }
   if (!strcmp(arglist, ")")) {
      // CINT accepted a single right paren as meaning no arguments.
      arglist = "";
   }
   *fMethod = info->GetMethodWithArgs(method, arglist, objectIsConst, poffset);
   if (!fMethod->IsValid()) {
      return;
   }
}

void TClingCallFunc::SetFunc(const TClingMethodInfo *info)
{
   Init(std::unique_ptr<TClingMethodInfo>(new TClingMethodInfo(*info)));
   if (!fMethod->IsValid()) {
      return;
   }
}

} // namespace CppyyLegacy

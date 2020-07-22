// @(#)root/utils:$Id$
// Author: Philippe Canal 27/08/2003

/*************************************************************************
 * Copyright (C) 1995-2003, Rene Brun, Fons Rademakers, and al.          *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS              *
 *************************************************************************/

#include "RConfigure.h"
#include <ROOT/RConfig.hxx>

#include "RStl.h"
#include "TClassEdit.h"
#include "TClingUtils.h"
using namespace CppyyLegacy::TClassEdit;

#include <stdio.h>

#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/LookupHelper.h"

#include "clang/Sema/Sema.h"
#include "clang/Sema/Template.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/DeclTemplate.h"

#include "Varargs.h"

//
// CppyyLegacy::Internal::RStl is the rootcint STL handling class.
//

namespace CppyyLegacy {

static int fgCount = 0;

Internal::RStl& Internal::RStl::Instance()
{
   // Return the singleton Internal::RStl.

   static Internal::RStl instance;
   return instance;

}

void Internal::RStl::GenerateTClassFor(const clang::QualType &type, const cling::Interpreter &interp, const TMetaUtils::TNormalizedCtxt &normCtxt)
{
   // Force the generation of the TClass for the given class.

   clang::QualType thisType = type;

   auto typePtr = thisType.getTypePtr();
   const clang::CXXRecordDecl *stlclass = typePtr->getAsCXXRecordDecl();
   if (!stlclass) {
      return;
   }

   // Transform the type to the corresponding one for IO
   auto typeForIO = TMetaUtils::GetTypeForIO(thisType, interp, normCtxt);
   if (typeForIO.getTypePtr() != typePtr)
      stlclass = typeForIO->getAsCXXRecordDecl();
   if (!stlclass) {
      return;
   }
   thisType = typeForIO;

   const clang::ClassTemplateSpecializationDecl *templateCl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(stlclass);

   if (!templateCl) {
      TMetaUtils::Error("RStl::GenerateTClassFor","%s not in a template",
            TMetaUtils::GetQualifiedName(*stlclass).c_str());
   }

   if ( TClassEdit::STLKind( stlclass->getName().str() )  == kSTLvector ) {
      const clang::TemplateArgument &arg( templateCl->getTemplateArgs().get(0) );
      if (arg.getKind() == clang::TemplateArgument::Type) {
         const clang::NamedDecl *decl = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
         if (decl) {
            // NOTE: we should just compare the decl to the bool builtin!
            llvm::StringRef argname = decl->getName();
            if ( (argname.str() == "bool") || (argname.str() == "Bool_t") ) {
               TMetaUtils::Warning("std::vector<bool>", " is not fully supported yet!\nUse std::vector<char> or std::deque<bool> instead.\n");
            }
         }
      }
   }

   fList.insert( TMetaUtils::AnnotatedRecordDecl(++fgCount,
                                                       thisType.getTypePtr(),
                                                       stlclass,
                                                       "",
                                                       false /* for backward compatibility rather than 'true' .. neither really make a difference */,
                                                       false,
                                                       false,
                                                       false,
                                                       -1,
                                                       interp,
                                                       normCtxt) );

   // fprintf(stderr,"Registered the STL class %s as needing a dictionary\n",R__GetQualifiedName(*stlclass).c_str());

   for(unsigned int i=0; i <  templateCl->getTemplateArgs().size(); ++i) {
      const clang::TemplateArgument &arg( templateCl->getTemplateArgs().get(i) );
      if (arg.getKind() == clang::TemplateArgument::Type) {
         const clang::NamedDecl *decl = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();

         if (decl && TClassEdit::STLKind( decl->getName().str() ) != 0 )
            {
               const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
               if (clxx) {
                  if (!clxx->isCompleteDefinition()) {
                     /* bool result = */ TMetaUtils::RequireCompleteType(interp, clxx->getLocation(), arg.getAsType());
                  }
                  // Do we need to strip the qualifier?
                  GenerateTClassFor(arg.getAsType(),interp,normCtxt);
               }
            }
      }
   }
}

void Internal::RStl::GenerateTClassFor(const char *requestedName, const clang::CXXRecordDecl *stlclass, const cling::Interpreter &interp, const TMetaUtils::TNormalizedCtxt &normCtxt)
{
   // Force the generation of the TClass for the given class.
   const clang::ClassTemplateSpecializationDecl *templateCl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(stlclass);

   if (templateCl == 0) {
      TMetaUtils::Error("RStl::GenerateTClassFor","%s not in a template",
            TMetaUtils::GetQualifiedName(*stlclass).c_str());
   }


   if ( TClassEdit::STLKind( stlclass->getName().str() )  == kSTLvector ) {
      const clang::TemplateArgument &arg( templateCl->getTemplateArgs().get(0) );
      if (arg.getKind() == clang::TemplateArgument::Type) {
         const clang::NamedDecl *decl = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
         if (decl) {
            // NOTE: we should just compare the decl to the bool builtin!
            llvm::StringRef argname = decl->getName();
            if ( (argname.str() == "bool") || (argname.str() == "Bool_t") ) {
               TMetaUtils::Warning("std::vector<bool>", " is not fully supported yet!\nUse std::vector<char> or std::deque<bool> instead.\n");
            }
         }
      }
   }

   fList.insert( TMetaUtils::AnnotatedRecordDecl(++fgCount,stlclass,requestedName,true,false,false,false,-1, interp,normCtxt) );

   TClassEdit::TSplitType splitType( requestedName );
   for(unsigned int i=0; i <  templateCl->getTemplateArgs().size(); ++i) {
      const clang::TemplateArgument &arg( templateCl->getTemplateArgs().get(i) );
      if (arg.getKind() == clang::TemplateArgument::Type) {
         const clang::NamedDecl *decl = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();

         if (decl && TClassEdit::STLKind( decl->getName().str() ) != 0 )
            {
               const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
               if (clxx) {
                  if (!clxx->isCompleteDefinition()) {
                     /* bool result = */ TMetaUtils::RequireCompleteType(interp, clxx->getLocation (), arg.getAsType());
                     clxx = arg.getAsType().getTypePtr()->getAsCXXRecordDecl();
                  }
                  if (!splitType.fElements.empty()) {
                     GenerateTClassFor( splitType.fElements[i+1].c_str(), clxx, interp, normCtxt);
                  } else {
                     GenerateTClassFor( "", clxx, interp, normCtxt );
                  }
               }
            }
      }
   }

}

void Internal::RStl::WriteClassInit(std::ostream &ostr,
                                const cling::Interpreter &interp,
                                const TMetaUtils::TNormalizedCtxt &normCtxt,
                                const TMetaUtils::RConstructorTypes& ctorTypes,
                                bool &needCollectionProxy,
                                void (*emitStreamerInfo)(const char*) )
{
   // This function writes the TGeneraticClassInfo initialiser
   // and the auxiliary functions (new and delete wrappers) for
   // each of the STL containers that have been registered

   list_t::iterator iter;
   for(iter = fList.begin(); iter != fList.end(); ++iter) {
      const clang::CXXRecordDecl* result;

      if (!iter->GetRecordDecl()->getDefinition()) {

         // We do not have a complete definition, we need to force the instantiation
         // and findScope can do that.
         const cling::LookupHelper& lh = interp.getLookupHelper();
         result = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(lh.findScope(iter->GetNormalizedName(),
                                                                            cling::LookupHelper::NoDiagnostics,
                                                                            0)
                                                               );

         if (!result || !iter->GetRecordDecl()->getDefinition()) {
            fprintf(stderr,"Error: incomplete definition for %s\n",iter->GetNormalizedName());
            continue;
         }
      }
      else
      {
         result = llvm::dyn_cast<clang::CXXRecordDecl>(iter->GetRecordDecl());
      }

      TMetaUtils::WriteClassInit(ostr, *iter, result, interp, normCtxt, ctorTypes, needCollectionProxy);
      TMetaUtils::WriteAuxFunctions(ostr, *iter, result, interp, ctorTypes, normCtxt);

      if (emitStreamerInfo) emitStreamerInfo(iter->GetNormalizedName());
   }
}

} // namespace CppyyLegacy

// Authors: Axel Naumann, Philippe Canal, Danilo Piparo

/*************************************************************************
 * Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

const char *shortHelp =
   "Usage: rootcling [-v][-v0-4] [-f] [out.cxx] [opts] "
   "file1.h[+][-][!] file2.h[+][-][!] ...[LinkDef.h]\n";

#include "rootcling_impl.h"
#include "rootclingCommandLineOptionsHelp.h"

#include "RConfigure.h"
#include <ROOT/RConfig.hxx>
#include <ROOT/FoundationUtils.hxx>

#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#include <algorithm>
#include <stdio.h>

#include <errno.h>
#include <string>
#include <list>
#include <vector>
#include <sstream>
#include <iterator>
#include <map>
#include <fstream>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <numeric>


#ifdef _WIN32
#ifdef system
#undef system
#endif
#undef UNICODE
#include <windows.h>
#include <Tlhelp32.h> // for MAX_MODULE_NAME32
#include <process.h>
#define PATH_MAX _MAX_PATH
#ifdef interface
// prevent error coming from clang/AST/Attrs.inc
#undef interface
#endif
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#if !defined(R__WIN32)
#include <limits.h>
#include <unistd.h>
#endif


#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/Interpreter/LookupHelper.h"
#include "cling/Interpreter/Value.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Mangle.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/Pragma.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTWriter.h"
#include "cling/Utils/AST.h"

#include "llvm/ADT/StringRef.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Process.h"

#include "RtypesCore.h"
#include "TModuleGenerator.h"
#include "TClassEdit.h"
#include "TClingUtils.h"
#include "RStl.h"
#include "XMLReader.h"
#include "LinkdefReader.h"
#include "DictSelectionReader.h"
#include "SelectionRules.h"
#include "Scanner.h"
#include "strlcpy.h"

#include "OptionParser.h"

#ifdef WIN32
const std::string gLibraryExtension(".dll");
#else
const std::string gLibraryExtension(".so");  // no dylib for the moment
#endif
const std::string gPathSeparator(CppyyLegacy::TMetaUtils::GetPathSeparator());

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#if defined(R__WIN32)
#include "cygpath.h"
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <unistd.h>
#endif


using namespace CppyyLegacy;

bool gBuildingROOT = false;
const CppyyLegacy::Internal::RootCling::DriverConfig* gDriverConfig = nullptr;


// Maybe too ugly? let's see how it performs.
using HeadersDeclsMap_t = std::map<std::string, std::list<std::string>>;

using namespace TClassEdit;

namespace std {}
using namespace std;

namespace genreflex {
   bool verbose = false;
}

////////////////////////////////////////////////////////////////////////////////

void SetRootSys();

CppyyLegacy::Internal::RootCling::TROOTSYSSetter::TROOTSYSSetter() {
   // rootcling's libCoreLegacy needs "our" ROOTSYS:
   SetRootSys();
};

////////////////////////////////////////////////////////////////////////////////

void EmitStreamerInfo(const char *normName)
{
   if (gDriverConfig->fAddStreamerInfoToROOTFile)
      gDriverConfig->fAddStreamerInfoToROOTFile(normName);
}
static void EmitTypedefs(const std::vector<const clang::TypedefNameDecl *> &tdvec)
{
   if (!gDriverConfig->fAddTypedefToROOTFile)
      return;
   for (const auto td : tdvec)
      gDriverConfig->fAddTypedefToROOTFile(td->getQualifiedNameAsString().c_str());
}
static void EmitEnums(const std::vector<const clang::EnumDecl *> &enumvec)
{
   if (!gDriverConfig->fAddEnumToROOTFile)
      return;
   for (const auto en : enumvec) {
      // Enums within tag decls are processed as part of the tag.
      if (clang::isa<clang::TranslationUnitDecl>(en->getDeclContext())
            || clang::isa<clang::LinkageSpecDecl>(en->getDeclContext())
            || clang::isa<clang::NamespaceDecl>(en->getDeclContext()))
         gDriverConfig->fAddEnumToROOTFile(en->getQualifiedNameAsString().c_str());
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the executable path name, used e.g. by SetRootSys().

const char *GetExePath()
{
  static std::string exepath;
  if (exepath == "") {
#ifdef __APPLE__
    exepath = _dyld_get_image_name(0);
#endif
#if defined(__linux) || defined(__linux__)
    char linkname[PATH_MAX];  // /proc/<pid>/exe
    char buf[PATH_MAX];     // exe path name
    pid_t pid;

    // get our pid and build the name of the link in /proc
    pid = getpid();
    snprintf(linkname, PATH_MAX, "/proc/%i/exe", pid);
    int ret = readlink(linkname, buf, 1024);
    if (ret > 0 && ret < 1024) {
      buf[ret] = 0;
      exepath = buf;
    }
#endif
#ifdef _WIN32
    char *buf = new char[MAX_MODULE_NAME32 + 1];
      ::GetModuleFileName(NULL, buf, MAX_MODULE_NAME32 + 1);
      char *p = buf;
      while ((p = strchr(p, '\\')))
         * (p++) = '/';
      exepath = buf;
      delete[] buf;
#endif
  }
  return exepath.c_str();
}

////////////////////////////////////////////////////////////////////////////////

bool Namespace__HasMethod(const clang::NamespaceDecl *cl, const char *name,
                          const cling::Interpreter &interp)
{
   return TMetaUtils::ClassInfo__HasMethod(cl, name, interp);
}

////////////////////////////////////////////////////////////////////////////////

static void AnnotateFieldDecl(clang::FieldDecl &decl,
                              const std::list<VariableSelectionRule> &fieldSelRules)
{
   using namespace CppyyLegacy::TMetaUtils;
   // See if in the VariableSelectionRules there are attributes and names with
   // which we can annotate.
   // We may look for a smarter algorithm.

   // Nothing to do then ...
   if (fieldSelRules.empty()) return;

   clang::ASTContext &C = decl.getASTContext();

   const std::string declName(decl.getNameAsString());
   std::string varName;
   for (std::list<VariableSelectionRule>::const_iterator it = fieldSelRules.begin();
         it != fieldSelRules.end(); ++it) {
      if (! it->GetAttributeValue(propNames::name, varName)) continue;
      if (declName == varName) { // we have the rule!
         // Let's extract the attributes
         BaseSelectionRule::AttributesMap_t attrMap(it->GetAttributes());
         BaseSelectionRule::AttributesMap_t::iterator iter;
         std::string userDefinedProperty;
         for (iter = attrMap.begin(); iter != attrMap.end(); ++iter) {
            const std::string &name = iter->first;
            const std::string &value = iter->second;

            if (name == propNames::name) continue;

            /* This test is here since in ROOT5, when using genreflex,
             * for pods, iotype is ignored */

            if (name == propNames::iotype &&
                  (decl.getType()->isArrayType() ||  decl.getType()->isPointerType())) {
               const char *msg = "Data member \"%s\" is an array or a pointer. "
                                 "It is not possible to assign to it the iotype \"%s\". "
                                 "This transformation is possible only with data members "
                                 "which are not pointers or arrays.\n";
               ::CppyyLegacy::TMetaUtils::Error("AnnotateFieldDecl",
                                       msg, varName.c_str(), value.c_str());
               continue;
            }


            // These lines are here to use the root pcms. Indeed we need to annotate the AST
            // before persisting the ProtoClasses in the root pcms.
            // BEGIN ROOT PCMS
            if (name == propNames::comment) {
               decl.addAttr(clang::AnnotateAttr::CreateImplicit(C, value));
            }
            // END ROOT PCMS

            if ((name == propNames::transient && value == "true") ||
                  (name == propNames::persistent && value == "false")) { // special case
               userDefinedProperty = propNames::comment + propNames::separator + "!";
               // This next line is here to use the root pcms. Indeed we need to annotate the AST
               // before persisting the ProtoClasses in the root pcms.
               // BEGIN ROOT PCMS
               decl.addAttr(clang::AnnotateAttr::CreateImplicit(C, "!"));
               // END ROOT PCMS
               // The rest of the lines are not changed to leave in place the system which
               // works with bulk header parsing on library load.
            } else {
               userDefinedProperty = name + propNames::separator + value;
            }
            ::CppyyLegacy::TMetaUtils::Info(0, "%s %s\n", varName.c_str(), userDefinedProperty.c_str());
            decl.addAttr(clang::AnnotateAttr::CreateImplicit(C, userDefinedProperty));

         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////

void AnnotateDecl(clang::CXXRecordDecl &CXXRD,
                  const RScanner::DeclsSelRulesMap_t &declSelRulesMap,
                  cling::Interpreter &interpreter,
                  bool isGenreflex)
{
   // In order to store the meaningful for the IO comments we have to transform
   // the comment into annotation of the given decl.
   // This works only with comments in the headers, so no selection rules in an
   // xml file.

   using namespace clang;
   SourceLocation commentSLoc;
   llvm::StringRef comment;

   ASTContext &C = CXXRD.getASTContext();
   Sema &S = interpreter.getCI()->getSema();

   SourceRange commentRange;

   // Fetch the selection rule associated to this class
   clang::Decl *declBaseClassPtr = static_cast<clang::Decl *>(&CXXRD);
   auto declSelRulePair = declSelRulesMap.find(declBaseClassPtr->getCanonicalDecl());
   if (declSelRulePair == declSelRulesMap.end()){
      const std::string thisClassName(CXXRD.getName());
      ::CppyyLegacy::TMetaUtils::Error("AnnotateDecl","Cannot find class %s in the list of selected classes.\n",thisClassName.c_str());
      return;
   }
   const BaseSelectionRule *thisClassBaseSelectionRule = declSelRulePair->second;
   // If the rule is there
   if (thisClassBaseSelectionRule) {
      // Fetch and loop over Class attributes
      // if the name of the attribute is not "name", add attr to the ast.
      BaseSelectionRule::AttributesMap_t::iterator iter;
      std::string userDefinedProperty;
      for (auto const & attr : thisClassBaseSelectionRule->GetAttributes()) {
         const std::string &name = attr.first;
         if (name == TMetaUtils::propNames::name) continue;
         const std::string &value = attr.second;
         userDefinedProperty = name + TMetaUtils::propNames::separator + value;
         if (genreflex::verbose) std::cout << " * " << userDefinedProperty << std::endl;
         CXXRD.addAttr(AnnotateAttr::CreateImplicit(C, userDefinedProperty));
      }
   }

   // See if the rule is a class selection rule (FIX dynamic_cast)
   const ClassSelectionRule *thisClassSelectionRule = reinterpret_cast<const ClassSelectionRule *>(thisClassBaseSelectionRule);

   for (CXXRecordDecl::decl_iterator I = CXXRD.decls_begin(),
         E = CXXRD.decls_end(); I != E; ++I) {

      // CXXMethodDecl,FieldDecl and VarDecl inherit from NamedDecl
      // See: http://clang.llvm.org/doxygen/classclang_1_1DeclaratorDecl.html
      if (!(*I)->isImplicit()
            && (isa<CXXMethodDecl>(*I) || isa<FieldDecl>(*I) || isa<VarDecl>(*I))) {


         // For now we allow only a special macro (ClassDef) to have meaningful comments
         SourceLocation maybeMacroLoc = (*I)->getLocation();
         bool isClassDefMacro = maybeMacroLoc.isMacroID() && S.findMacroSpelling(maybeMacroLoc, "ClassDef");
         if (isClassDefMacro) {
            while (isa<NamedDecl>(*I) && cast<NamedDecl>(*I)->getName() != "DeclFileLine") {
               ++I;
            }
         }

         comment = TMetaUtils::GetComment(**I, &commentSLoc);
         if (comment.size()) {
            // Keep info for the source range of the comment in case we want to issue
            // nice warnings, eg. empty comment and so on.
            commentRange = SourceRange(commentSLoc, commentSLoc.getLocWithOffset(comment.size()));
            // The ClassDef annotation is for the class itself
            if (isClassDefMacro) {
               CXXRD.addAttr(AnnotateAttr::CreateImplicit(C, comment.str()));
            } else if (!isGenreflex) {
               // Here we check if we are in presence of a selection file so that
               // the comment does not ends up as a decoration in the AST,
               // Nevertheless, w/o PCMS this has no effect, since the headers
               // are parsed at runtime and the information in the AST dumped by
               // rootcling is not relevant.
               (*I)->addAttr(AnnotateAttr::CreateImplicit(C, comment.str()));
            }
         }
         // Match decls with sel rules if we are in presence of a selection file
         // and the cast was successful
         if (isGenreflex && thisClassSelectionRule != 0) {
            const std::list<VariableSelectionRule> &fieldSelRules = thisClassSelectionRule->GetFieldSelectionRules();

            // This check is here to avoid asserts in debug mode (LLVMDEV env variable set)
            if (FieldDecl *fieldDecl  = dyn_cast<FieldDecl>(*I)) {
               AnnotateFieldDecl(*fieldDecl, fieldSelRules);
            }
         } // End presence of XML selection file
      }
   }
}

////////////////////////////////////////////////////////////////////////////////

size_t GetFullArrayLength(const clang::ConstantArrayType *arrayType)
{
   llvm::APInt len = arrayType->getSize();
   while (const clang::ConstantArrayType *subArrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual())) {
      len *= subArrayType->getSize();
      arrayType = subArrayType;
   }
   return len.getLimitedValue();
}

////////////////////////////////////////////////////////////////////////////////

bool InheritsFromTObject(const clang::RecordDecl *cl,
                         const cling::Interpreter &interp)
{
   static const clang::CXXRecordDecl *TObject_decl
      = TMetaUtils::ScopeSearch("::CppyyLegacy::TObject", interp, true /*diag*/, 0);

   const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(cl);
   return TMetaUtils::IsBase(clxx, TObject_decl, nullptr, interp);
}

////////////////////////////////////////////////////////////////////////////////

bool IsSelectionXml(const char *filename)
{
   size_t len = strlen(filename);
   size_t xmllen = 4; /* strlen(".xml"); */
   if (strlen(filename) >= xmllen) {
      return (0 == strcasecmp(filename + (len - xmllen), ".xml"));
   } else {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////

bool IsLinkdefFile(const clang::PresumedLoc& PLoc)
{
   return TMetaUtils::IsLinkdefFile(PLoc.getFilename());
}

////////////////////////////////////////////////////////////////////////////////

bool IsSelectionFile(const char *filename)
{
   return TMetaUtils::IsLinkdefFile(filename) || IsSelectionXml(filename);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the ROOTSYS env var based on the executable location.

void SetRootSys()
{
   const char *exepath = GetExePath();
   if (exepath && *exepath) {
#if !defined(_WIN32)
      char *ep = new char[PATH_MAX];
      if (!realpath(exepath, ep)) {
         fprintf(stderr, "rootcling: error getting realpath of rootcling!");
         strlcpy(ep, exepath, PATH_MAX);
      }
#else
      int nche = strlen(exepath) + 1;
      char *ep = new char[nche];
      strlcpy(ep, exepath, nche);
#endif
      char *s;

      if ((s = strrchr(ep, '/'))) {
         // $ROOTSYS/bin/rootcling
         int removesubdirs = 2;
         if (!strncmp(s + 1, "rootcling_stage1.exe", 20)) {
            // $ROOTSYS/bin/rootcling_stage1.exe
            removesubdirs = 2;
            gBuildingROOT = true;
         } else if (!strncmp(s + 1, "rootcling_stage1", 16)) {
            // $ROOTSYS/core/rootcling_stage1/src/rootcling_stage1
            removesubdirs = 4;
            gBuildingROOT = true;
         }
         for (int i = 1; s && i < removesubdirs; ++i) {
            *s = 0;
            s = strrchr(ep, '/');
         }
         if (s) *s = 0;
      } else {
         // There was no slashes at all let now change ROOTSYS
         delete[] ep;
         return;
      }

      if (!gBuildingROOT) {
         delete[] ep;
         return; // don't mess with user's ROOTSYS.
      }

      int ncha = strlen(ep) + 10;
      char *env = new char[ncha];
      snprintf(env, ncha, "ROOTSYS=%s", ep);

      if (gDriverConfig) {
         // After the putenv below, gRootDir might point to the old ROOTSYS
         // entry, i.e. to deleted memory. Update it.
         const char** pRootDir = gDriverConfig->fPRootDir;
         if (pRootDir) {
            *pRootDir = env + 8;
         }
      }

      putenv(env);
      delete [] ep;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Check whether the #pragma line contains expectedTokens (0-terminated array).

bool ParsePragmaLine(const std::string &line,
                     const char *expectedTokens[],
                     size_t *end = 0)
{
   if (end) *end = 0;
   if (line[0] != '#') return false;
   size_t pos = 1;
   for (const char **iToken = expectedTokens; *iToken; ++iToken) {
      while (isspace(line[pos])) ++pos;
      size_t lenToken = strlen(*iToken);
      if (line.compare(pos, lenToken, *iToken)) {
         if (end) *end = pos;
         return false;
      }
      pos += lenToken;
   }
   if (end) *end = pos;
   return true;
}


map<string, string> gAutoloads;
string gLibsNeeded;

////////////////////////////////////////////////////////////////////////////////

void RecordDeclCallback(const clang::RecordDecl* recordDecl)
{
   std::string need;
   if (recordDecl->hasOwningModule()) {
      clang::Module *M = recordDecl->getOwningModule()->getTopLevelModule();
      need = "lib" + M->Name + gLibraryExtension;
   } else {
      std::string qual_name;
      RScanner::GetDeclQualName(recordDecl, qual_name);

      need = gAutoloads[qual_name];
   }

   if (need.length() && gLibsNeeded.find(need) == string::npos) {
      gLibsNeeded += " " + need;
   }
}

////////////////////////////////////////////////////////////////////////////////

void CheckClassNameForRootMap(const std::string &classname, map<string, string> &autoloads)
{
   if (classname.find(':') == std::string::npos) return;

   // We have a namespace and we have to check it first
   int slen = classname.size();
   for (int k = 0; k < slen; ++k) {
      if (classname[k] == ':') {
         if (k + 1 >= slen || classname[k + 1] != ':') {
            // we expected another ':'
            break;
         }
         if (k) {
            string base = classname.substr(0, k);
            if (base == "std") {
               // std is not declared but is also ignored by CINT!
               break;
            } else {
               autoloads[base] = ""; // We never load namespaces on their own.
            }
            ++k;
         }
      } else if (classname[k] == '<') {
         // We do not want to look at the namespace inside the template parameters!
         break;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Parse the rootmap and add entries to the autoload map

void ParseRootMapFile(ifstream &file, map<string, string> &autoloads)
{
   std::string classname;
   std::string line;
   while (file >> line) {

      if (line.find("Library.") != 0) continue;

      int pos = line.find(":", 8);
      classname = line.substr(8, pos - 8);

      TMetaUtils::ReplaceAll(classname, "@@", "::");
      TMetaUtils::ReplaceAll(classname, "-", " ");

      getline(file, line, '\n');
      while (line[0] == ' ') line.replace(0, 1, "");

      CheckClassNameForRootMap(classname, autoloads);

      autoloads[classname] = line;
   }

}

////////////////////////////////////////////////////////////////////////////////
/// Parse the rootmap and add entries to the autoload map, using the new format

void ParseRootMapFileNewFormat(ifstream &file, map<string, string> &autoloads)
{
   std::string keyname;
   std::string libs;
   std::string line;

   // For "class ", "namespace " and "typedef " respectively
   const std::unordered_map<char, unsigned int> keyLenMap = {{'c', 6}, {'n', 10}, {'t', 8}};

   while (getline(file, line, '\n')) {
      if (line == "{ decls }") {
         while (getline(file, line, '\n')) {
            if (line[0] == '[') break;
         }
      }
      const char firstChar = line[0];
      if (firstChar == '[') {
         // new section
         libs = line.substr(1, line.find(']') - 1);
         while (libs[0] == ' ') libs.replace(0, 1, "");
      } else if (0 != keyLenMap.count(firstChar)) {
         unsigned int keyLen = keyLenMap.at(firstChar);
         keyname = line.substr(keyLen, line.length() - keyLen);
         CheckClassNameForRootMap(keyname, autoloads);
         autoloads[keyname] = libs;
      }
   }

}

////////////////////////////////////////////////////////////////////////////////
/// Fill the map of libraries to be loaded in presence of a class
/// Transparently support the old and new rootmap file format

void LoadLibraryMap(const std::string &fileListName, map<string, string> &autoloads)
{
   std::ifstream filelist(fileListName.c_str());

   std::string filename;
   std::string line;

   while (filelist >> filename) {

      if (llvm::sys::fs::is_directory(filename)) continue;

      ifstream file(filename.c_str());

      // Check which format is this
      file >> line;
      bool new_format = (line[0] == '[' || line[0] == '{') ;
      file.clear();
      file.seekg(0, std::ios::beg);

      // Now act
      if (new_format) {
         ParseRootMapFileNewFormat(file, autoloads);
      } else {
         ParseRootMapFile(file, autoloads);
      }

      file.close();

   } // end loop on files
   filelist.close();
}

////////////////////////////////////////////////////////////////////////////////
/// Check if the specified operator (what) has been properly declared if the user has
/// requested a custom version.

bool CheckInputOperator(const char *what,
                        const char *proto,
                        const string &fullname,
                        const clang::RecordDecl *cl,
                        cling::Interpreter &interp)
{

   const clang::FunctionDecl *method
      = CppyyLegacy::TMetaUtils::GetFuncWithProto(llvm::dyn_cast<clang::Decl>(cl->getDeclContext()), what, proto, interp,
                                           false /*diags*/);
   if (!method) {
      // This intended to find the global scope.
      clang::TranslationUnitDecl *TU =
         cl->getASTContext().getTranslationUnitDecl();
      method = TMetaUtils::GetFuncWithProto(TU, what, proto, interp,
               false /*diags*/);
   }
   bool has_input_error = false;
   if (method != 0 && (method->getAccess() == clang::AS_public || method->getAccess() == clang::AS_none)) {
      std::string filename = TMetaUtils::GetFileName(*method, interp);
      if (strstr(filename.c_str(), "TBuffer.h") != 0 ||
            strstr(filename.c_str(), "Rtypes.h") != 0) {

         has_input_error = true;
      }
   } else {
      has_input_error = true;
   }
   if (has_input_error) {
      // We don't want to generate duplicated error messages in several dictionaries (when generating temporaries)
      const char *maybeconst = "";
      const char *mayberef = "&";
      if (what[strlen(what) - 1] == '<') {
         maybeconst = "const ";
         mayberef = "";
      }
      ::CppyyLegacy::TMetaUtils::Error(0,
                              "in this version of ROOT, the option '!' used in a linkdef file\n"
                              "       implies the actual existence of customized operators.\n"
                              "       The following declaration is now required:\n"
                              "   CppyyLegacy::TBuffer &%s(CppyyLegacy::TBuffer &,%s%s *%s);\n", what, maybeconst, fullname.c_str(), mayberef);
   }
   return has_input_error;

}

////////////////////////////////////////////////////////////////////////////////
/// Check if the operator>> has been properly declared if the user has
/// requested a custom version.

bool CheckInputOperator(const clang::RecordDecl *cl, cling::Interpreter &interp)
{
   string fullname;
   TMetaUtils::GetQualifiedName(fullname, *cl);
   int ncha = fullname.length() + 26;
   char *proto = new char[ncha];
   snprintf(proto, ncha, "CppyyLegacy::TBuffer&,%s*&", fullname.c_str());

   TMetaUtils::Info(0, "Class %s: Do not generate operator>>()\n",
                          fullname.c_str());

   // We do want to call both CheckInputOperator all the times.
   bool has_input_error = CheckInputOperator("operator>>", proto, fullname, cl, interp);
   has_input_error = CheckInputOperator("operator<<", proto, fullname, cl, interp) || has_input_error;

   delete [] proto;

   return has_input_error;
}

////////////////////////////////////////////////////////////////////////////////
/// Return false if the class does not have ClassDef even-though it should.

bool CheckClassDef(const clang::RecordDecl &cl, const cling::Interpreter &interp)
{

   // Detect if the class has a ClassDef
   bool hasClassDef = TMetaUtils::ClassInfo__HasMethod(&cl, "Class_Version", interp);

   const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(&cl);
   if (!clxx) {
      return false;
   }
   bool isAbstract = clxx->isAbstract();

   if (!isAbstract && InheritsFromTObject(clxx, interp) && !hasClassDef) {
      std::string qualName;
      TMetaUtils::GetQualifiedName(qualName, cl);
      const char *qualName_c = qualName.c_str();
      TMetaUtils::Warning(qualName_c, "The data members of %s will not be stored, "
                                            "because it inherits from TObject but does not "
                                            "have its own ClassDef.\n",
                                qualName_c);
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the name of the data member so that it can be used
/// by non-const operation (so it includes a const_cast if necessary).

string GetNonConstMemberName(const clang::FieldDecl &m, const string &prefix = "")
{
   if (m.getType().isConstQualified()) {
      string ret = "const_cast< ";
      string type_name;
      TMetaUtils::GetQualifiedName(type_name, m.getType(), m);
      if (type_name.substr(0,6)=="const ") {
         ret += type_name.c_str()+6;
      } else {
         ret += type_name;
      }
      ret += " &>( ";
      ret += prefix;
      ret += m.getName().str();
      ret += " )";
      return ret;
   } else {
      return prefix + m.getName().str();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Create Streamer code for an STL container. Returns 1 if data member
/// was an STL container and if Streamer code has been created, 0 otherwise.

int STLContainerStreamer(const clang::FieldDecl &m,
                         int rwmode,
                         const cling::Interpreter &interp,
                         const CppyyLegacy::TMetaUtils::TNormalizedCtxt &normCtxt,
                         std::ostream &dictStream)
{
   ::CppyyLegacy::ESTLType stltype = TMetaUtils::IsSTLContainer(m);
   std::string mTypename;
   TMetaUtils::GetQualifiedName(mTypename, m.getType(), m);

   const clang::CXXRecordDecl *clxx = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(TMetaUtils::GetUnderlyingRecordDecl(m.getType()));

   if (stltype == ::CppyyLegacy::kNotSTL) {
      return 0;
   }
   //        fprintf(stderr,"Add %s (%d) which is also %s\n",
   //                m.Type()->Name(), stltype, m.Type()->TrueName() );
   clang::QualType utype(TMetaUtils::GetUnderlyingType(m.getType()), 0);
   Internal::RStl::Instance().GenerateTClassFor(utype, interp, normCtxt);

   if (!clxx || clxx->getTemplateSpecializationKind() == clang::TSK_Undeclared) return 0;

   const clang::ClassTemplateSpecializationDecl *tmplt_specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl> (clxx);
   if (!tmplt_specialization) return 0;


   string stlType(TMetaUtils::ShortTypeName(mTypename.c_str()));
   string stlName;
   stlName = TMetaUtils::ShortTypeName(m.getName().str().c_str());

   string fulName1, fulName2;
   const char *tcl1 = 0, *tcl2 = 0;
   const clang::TemplateArgument &arg0(tmplt_specialization->getTemplateArgs().get(0));
   clang::QualType ti = arg0.getAsType();

   if (TMetaUtils::ElementStreamer(dictStream, m, ti, 0, rwmode, interp)) {
      tcl1 = "R__tcl1";
      fulName1 = ti.getAsString(); // Should we be passing a context?
   }
   if (stltype == kSTLmap || stltype == kSTLmultimap) {
      const clang::TemplateArgument &arg1(tmplt_specialization->getTemplateArgs().get(1));
      clang::QualType tmplti = arg1.getAsType();
      if (TMetaUtils::ElementStreamer(dictStream, m, tmplti, 0, rwmode, interp)) {
         tcl2 = "R__tcl2";
         fulName2 = tmplti.getAsString(); // Should we be passing a context?
      }
   }

   int isArr = 0;
   int len = 1;
   int pa = 0;
   const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(m.getType().getTypePtr());
   if (arrayType) {
      isArr = 1;
      len =  GetFullArrayLength(arrayType);
      pa = 1;
      while (arrayType) {
         if (arrayType->getArrayElementTypeNoTypeQual()->isPointerType()) {
            pa = 3;
            break;
         }
         arrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual());
      }
   } else if (m.getType()->isPointerType()) {
      pa = 2;
   }
   if (rwmode == 0) {
      // create read code
      dictStream << "      {" << std::endl;
      if (isArr) {
         dictStream << "         for (Int_t R__l = 0; R__l < " << len << "; R__l++) {" << std::endl;
      }

      switch (pa) {
         case 0:         //No pointer && No array
            dictStream << "         " << stlType.c_str() << " &R__stl =  " << stlName.c_str() << ";" << std::endl;
            break;
         case 1:         //No pointer && array
            dictStream << "         " << stlType.c_str() << " &R__stl =  " << stlName.c_str() << "[R__l];" << std::endl;
            break;
         case 2:         //pointer && No array
            dictStream << "         delete *" << stlName.c_str() << ";" << std::endl
                       << "         *" << stlName.c_str() << " = new " << stlType.c_str() << ";" << std::endl
                       << "         " << stlType.c_str() << " &R__stl = **" << stlName.c_str() << ";" << std::endl;
            break;
         case 3:         //pointer && array
            dictStream << "         delete " << stlName.c_str() << "[R__l];" << std::endl
                       << "         " << stlName.c_str() << "[R__l] = new " << stlType.c_str() << ";" << std::endl
                       << "         " << stlType.c_str() << " &R__stl = *" << stlName.c_str() << "[R__l];" << std::endl;
            break;
      }

      dictStream << "         R__stl.clear();" << std::endl;

      if (tcl1) {
         dictStream << "         TClass *R__tcl1 = TBuffer::GetClass(typeid(" << fulName1.c_str() << "));" << std::endl
                    << "         if (R__tcl1==0) {" << std::endl
                    << "            Error(\"" << stlName.c_str() << " streamer\",\"Missing the TClass object for "
                    << fulName1.c_str() << "!\");"  << std::endl
                    << "            return;" << std::endl
                    << "         }" << std::endl;
      }
      if (tcl2) {
         dictStream << "         TClass *R__tcl2 = TBuffer::GetClass(typeid(" << fulName2.c_str() << "));" << std::endl
                    << "         if (R__tcl2==0) {" << std::endl
                    << "            Error(\"" << stlName.c_str() << " streamer\",\"Missing the TClass object for "
                    << fulName2.c_str() << "!\");" << std::endl
                    << "            return;" << std::endl
                    << "         }" << std::endl;
      }

      dictStream << "         int R__i, R__n;" << std::endl
                 << "         R__b >> R__n;" << std::endl;

      if (stltype == kSTLvector) {
         dictStream << "         R__stl.reserve(R__n);" << std::endl;
      }
      dictStream << "         for (R__i = 0; R__i < R__n; R__i++) {" << std::endl;

      TMetaUtils::ElementStreamer(dictStream, m, arg0.getAsType(), "R__t", rwmode, interp, tcl1);
      if (stltype == kSTLmap || stltype == kSTLmultimap) {     //Second Arg
         const clang::TemplateArgument &arg1(tmplt_specialization->getTemplateArgs().get(1));
         TMetaUtils::ElementStreamer(dictStream, m, arg1.getAsType(), "R__t2", rwmode, interp, tcl2);
      }

      /* Need to go from
         type R__t;
         R__t.Stream;
         vec.push_back(R__t);
         to
         vec.push_back(type());
         R__t_p = &(vec.last());
         *R__t_p->Stream;

      */
      switch (stltype) {

         case kSTLmap:
         case kSTLmultimap:
         case kSTLunorderedmap:
         case kSTLunorderedmultimap:{
               std::string keyName(ti.getAsString());
               dictStream << "            typedef " << keyName << " Value_t;" << std::endl
                          << "            std::pair<Value_t const, " << tmplt_specialization->getTemplateArgs().get(1).getAsType().getAsString() << " > R__t3(R__t,R__t2);" << std::endl
                          << "            R__stl.insert(R__t3);" << std::endl;
               //fprintf(fp, "            R__stl.insert(%s::value_type(R__t,R__t2));\n",stlType.c_str());
               break;
            }
         case kSTLset:
         case kSTLunorderedset:
         case kSTLunorderedmultiset:
         case kSTLmultiset:
            dictStream << "            R__stl.insert(R__t);" << std::endl;
            break;
         case kSTLvector:
         case kSTLlist:
         case kSTLdeque:
            dictStream << "            R__stl.push_back(R__t);" << std::endl;
            break;
         case kSTLforwardlist:
            dictStream << "            R__stl.push_front(R__t);" << std::endl;
            break;
         default:
            assert(0);
      }
      dictStream << "         }" << std::endl
                 << "      }" << std::endl;
      if (isArr) dictStream << "    }" << std::endl;

   } else {

      // create write code
      if (isArr) {
         dictStream << "         for (Int_t R__l = 0; R__l < " << len << "; R__l++) {" << std::endl;
      }
      dictStream << "      {" << std::endl;
      switch (pa) {
         case 0:         //No pointer && No array
            dictStream << "         " << stlType.c_str() << " &R__stl =  " << stlName.c_str() << ";" << std::endl;
            break;
         case 1:         //No pointer && array
            dictStream << "         " << stlType.c_str() << " &R__stl =  " << stlName.c_str() << "[R__l];" << std::endl;
            break;
         case 2:         //pointer && No array
            dictStream << "         " << stlType.c_str() << " &R__stl = **" << stlName.c_str() << ";" << std::endl;
            break;
         case 3:         //pointer && array
            dictStream << "         " << stlType.c_str() << " &R__stl = *" << stlName.c_str() << "[R__l];" << std::endl;
            break;
      }

      dictStream << "         int R__n=int(R__stl.size());" << std::endl
                 << "         R__b << R__n;" << std::endl
                 << "         if(R__n) {" << std::endl;

      if (tcl1) {
         dictStream << "         TClass *R__tcl1 = TBuffer::GetClass(typeid(" << fulName1.c_str() << "));" << std::endl
                    << "         if (R__tcl1==0) {" << std::endl
                    << "            Error(\"" << stlName.c_str() << " streamer\",\"Missing the TClass object for "
                    << fulName1.c_str() << "!\");" << std::endl
                    << "            return;" << std::endl
                    << "         }" << std::endl;
      }
      if (tcl2) {
         dictStream << "         TClass *R__tcl2 = TBuffer::GetClass(typeid(" << fulName2.c_str() << "));" << std::endl
                    << "         if (R__tcl2==0) {" << std::endl
                    << "            Error(\"" << stlName.c_str() << "streamer\",\"Missing the TClass object for " << fulName2.c_str() << "!\");" << std::endl
                    << "            return;" << std::endl
                    << "         }" << std::endl;
      }

      dictStream << "            " << stlType.c_str() << "::iterator R__k;" << std::endl
                 << "            for (R__k = R__stl.begin(); R__k != R__stl.end(); ++R__k) {" << std::endl;
      if (stltype == kSTLmap || stltype == kSTLmultimap) {
         const clang::TemplateArgument &arg1(tmplt_specialization->getTemplateArgs().get(1));
         clang::QualType tmplti = arg1.getAsType();
         TMetaUtils::ElementStreamer(dictStream, m, ti, "((*R__k).first )", rwmode, interp, tcl1);
         TMetaUtils::ElementStreamer(dictStream, m, tmplti, "((*R__k).second)", rwmode, interp, tcl2);
      } else {
         TMetaUtils::ElementStreamer(dictStream, m, ti, "(*R__k)"         , rwmode, interp, tcl1);
      }

      dictStream << "            }" << std::endl
                 << "         }" << std::endl
                 << "      }" << std::endl;
      if (isArr) dictStream << "    }" << std::endl;
   }
   return 1;
}

////////////////////////////////////////////////////////////////////////////////
/// Create Streamer code for a standard string object. Returns 1 if data
/// member was a standard string and if Streamer code has been created,
/// 0 otherwise.

int STLStringStreamer(const clang::FieldDecl &m, int rwmode, std::ostream &dictStream)
{
   std::string mTypenameStr;
   TMetaUtils::GetQualifiedName(mTypenameStr, m.getType(), m);
   // Note: here we could to a direct type comparison!
   const char *mTypeName = TMetaUtils::ShortTypeName(mTypenameStr.c_str());
   if (!strcmp(mTypeName, "std::string")) {

      std::string fieldname =  m.getName().str();
      if (rwmode == 0) {
         // create read mode
         if (m.getType()->isConstantArrayType()) {
            if (m.getType().getTypePtr()->getArrayElementTypeNoTypeQual()->isPointerType()) {
               dictStream << "// Array of pointer to std::string are not supported (" << fieldname << "\n";
            } else {
               std::stringstream fullIdx;
               const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(m.getType().getTypePtr());
               int dim = 0;
               while (arrayType) {
                  dictStream << "      for (int R__i" << dim << "=0; R__i" << dim << "<"
                             << arrayType->getSize().getLimitedValue() << "; ++R__i" << dim << " )" << std::endl;
                  fullIdx << "[R__i" << dim << "]";
                  arrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual());
                  ++dim;
               }
               dictStream << "         { TString R__str; R__str.Streamer(R__b); "
                          << fieldname << fullIdx.str() << " = R__str.Data();}" << std::endl;
            }
         } else {
            dictStream << "      { TString R__str; R__str.Streamer(R__b); ";
            if (m.getType()->isPointerType())
               dictStream << "if (*" << fieldname << ") delete *" << fieldname << "; (*"
                          << fieldname << " = new string(R__str.Data())); }" << std::endl;
            else
               dictStream << fieldname << " = R__str.Data(); }" << std::endl;
         }
      } else {
         // create write mode
         if (m.getType()->isPointerType())
            dictStream << "      { TString R__str; if (*" << fieldname << ") R__str = (*"
                       << fieldname << ")->c_str(); R__str.Streamer(R__b);}" << std::endl;
         else if (m.getType()->isConstantArrayType()) {
            std::stringstream fullIdx;
            const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(m.getType().getTypePtr());
            int dim = 0;
            while (arrayType) {
               dictStream << "      for (int R__i" << dim << "=0; R__i" << dim << "<"
                          << arrayType->getSize().getLimitedValue() << "; ++R__i" << dim << " )" << std::endl;
               fullIdx << "[R__i" << dim << "]";
               arrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual());
               ++dim;
            }
            dictStream << "         { TString R__str(" << fieldname << fullIdx.str() << ".c_str()); R__str.Streamer(R__b);}" << std::endl;
         } else
            dictStream << "      { TString R__str = " << fieldname << ".c_str(); R__str.Streamer(R__b);}" << std::endl;
      }
      return 1;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////

bool isPointerToPointer(const clang::FieldDecl &m)
{
   if (m.getType()->isPointerType()) {
      if (m.getType()->getPointeeType()->isPointerType()) {
         return true;
      }
   }
   return false;
}

////////////////////////////////////////////////////////////////////////////////
/// Write "[0]" for all but the 1st dimension.

void WriteArrayDimensions(const clang::QualType &type, std::ostream &dictStream)
{
   const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(type.getTypePtr());
   if (arrayType) {
      arrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual());
      while (arrayType) {
         dictStream << "[0]";
         arrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType->getArrayElementTypeNoTypeQual());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Write the code to set the class name and the initialization object.

void WriteClassFunctions(const clang::CXXRecordDecl *cl, std::ostream &dictStream, bool autoLoad = false)
{
   bool add_template_keyword = TMetaUtils::NeedTemplateKeyword(cl);

   string fullname;
   string clsname;
   string nsname;
   int enclSpaceNesting = 0;

   if (TMetaUtils::GetNameWithinNamespace(fullname, clsname, nsname, cl)) {
      enclSpaceNesting = TMetaUtils::WriteNamespaceHeader(dictStream, cl);
   }

   if (autoLoad)
      dictStream << "#include \"TInterpreter.h\"\n";

   dictStream << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "atomic_TClass_ptr " << clsname << "::fgIsA(0);  // static to hold class pointer" << std::endl
              << std::endl

              << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "const char *" << clsname << "::Class_Name()" << std::endl << "{" << std::endl
              << "   return \"" << fullname << "\";"  << std::endl << "}" << std::endl << std::endl;

   dictStream << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "const char *" << clsname << "::ImplFileName()"  << std::endl << "{" << std::endl
              << "   return ::CppyyLegacy::GenerateInitInstanceLocal((const ::" << fullname
              << "*)0x0)->GetImplFileName();" << std::endl << "}" << std::endl << std::endl

              << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "int " << clsname << "::ImplFileLine()" << std::endl << "{" << std::endl
              << "   return ::CppyyLegacy::GenerateInitInstanceLocal((const ::" << fullname
              << "*)0x0)->GetImplFileLine();" << std::endl << "}" << std::endl << std::endl

              << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "TClass *" << clsname << "::Dictionary()" << std::endl << "{" << std::endl;

   // Trigger autoloading if dictionary is split
   if (autoLoad)
      dictStream << "   gInterpreter->AutoLoad(\"" << fullname << "\");\n";
   dictStream    << "   fgIsA = ::CppyyLegacy::GenerateInitInstanceLocal((const ::" << fullname
                 << "*)0x0)->GetClass();" << std::endl
                 << "   return fgIsA;\n"
                 << "}" << std::endl << std::endl

                 << "//_______________________________________"
                 << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "TClass *" << clsname << "::Class()" << std::endl << "{" << std::endl;
   if (autoLoad) {
      dictStream << "   Dictionary();\n";
   } else {
      dictStream << "   if (!fgIsA.load()) { R__LOCKGUARD(gInterpreterMutex); fgIsA = ::CppyyLegacy::GenerateInitInstanceLocal((const ::";
      dictStream << fullname << "*)0x0)->GetClass(); }" << std::endl;
   }
   dictStream    << "   return fgIsA;" << std::endl
                 << "}" << std::endl << std::endl;

   while (enclSpaceNesting) {
      dictStream << "} // namespace " << nsname << std::endl;
      --enclSpaceNesting;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Write the code to initialize the namespace name and the initialization object.

void WriteNamespaceInit(const clang::NamespaceDecl *cl,
                        cling::Interpreter &interp,
                        std::ostream &dictStream)
{
   if (cl->isAnonymousNamespace()) {
      // Don't write a GenerateInitInstance for the anonymous namespaces.
      return;
   }

   // coverity[fun_call_w_exception] - that's just fine.
   string classname = TMetaUtils::GetQualifiedName(*cl).c_str();
   string mappedname;
   TMetaUtils::GetCppName(mappedname, classname.c_str());

   int nesting = 0;
   // We should probably unwind the namespace to properly nest it.
   if (classname != "CppyyLegacy") {
      nesting = TMetaUtils::WriteNamespaceHeader(dictStream,cl);
   }

   dictStream << "   namespace CppyyLegacyX {" << std::endl;
   dictStream << "      using namespace CppyyLegacy;" << std::endl;

#if !defined(R__AIX)
   dictStream << "      inline TGenericClassInfo *GenerateInitInstance();" << std::endl;
#endif

   if (!Namespace__HasMethod(cl, "Dictionary", interp))
      dictStream << "      static TClass *" << mappedname.c_str() << "_Dictionary();" << std::endl;
   dictStream << std::endl

              << "      // Function generating the singleton type initializer" << std::endl

#if !defined(R__AIX)
              << "      inline TGenericClassInfo *GenerateInitInstance()" << std::endl
              << "      {" << std::endl
#else
              << "      TGenericClassInfo *GenerateInitInstance()" << std::endl
              << "      {" << std::endl
#endif

              << "         static TGenericClassInfo " << std::endl

              << "            instance(\"" << classname.c_str() << "\", ";

   if (Namespace__HasMethod(cl, "Class_Version", interp)) {
      dictStream << "::" << classname.c_str() << "::Class_Version(), ";
   } else {
      dictStream << "0 /*version*/, ";
   }

   std::string filename = TMetaUtils::GetFileName(*cl, interp);
   for (unsigned int i = 0; i < filename.length(); i++) {
      if (filename[i] == '\\') filename[i] = '/';
   }
   dictStream << "\"" << filename << "\", " << TMetaUtils::GetLineNumber(cl) << "," << std::endl
              << "                     Internal::DefineBehavior((void*)0,(void*)0)," << std::endl
              << "                     ";

   if (Namespace__HasMethod(cl, "Dictionary", interp)) {
      dictStream << "&::" << classname.c_str() << "::Dictionary, ";
   } else {
      dictStream << "&" << mappedname.c_str() << "_Dictionary, ";
   }

   dictStream << 0 << ");" << std::endl

              << "         return &instance;" << std::endl
              << "      }" << std::endl
              << "      // Insure that the inline function is _not_ optimized away by the compiler\n"
              << "      TGenericClassInfo *(*_R__UNIQUE_DICT_(InitFunctionKeeper))() = &GenerateInitInstance;  " << std::endl
              << "      // Static variable to force the class initialization" << std::endl
              // must be one long line otherwise R__UseDummy does not work
              << "      static TGenericClassInfo *_R__UNIQUE_DICT_(Init) = GenerateInitInstance();"
              << " R__UseDummy(_R__UNIQUE_DICT_(Init));" << std::endl;

   if (!Namespace__HasMethod(cl, "Dictionary", interp)) {
      dictStream <<  std::endl << "      // Dictionary for non-ClassDef classes" << std::endl
                 << "      static TClass *" << mappedname.c_str() << "_Dictionary() {" << std::endl
                 << "         return GenerateInitInstance()->GetClass();" << std::endl
                 << "      }" << std::endl << std::endl;
   }

   dictStream << "   }" << std::endl;
   while (nesting--) {
      dictStream << "}" << std::endl;
   }
   dictStream <<  std::endl;
}

////////////////////////////////////////////////////////////////////////////////
/// GrabIndex returns a static string (so use it or copy it immediately, do not
/// call GrabIndex twice in the same expression) containing the size of the
/// array data member.
/// In case of error, or if the size is not specified, GrabIndex returns 0.

llvm::StringRef GrabIndex(const cling::Interpreter& interp, const clang::FieldDecl &member, int printError)
{
   int error;
   llvm::StringRef where;

   llvm::StringRef index = TMetaUtils::DataMemberInfo__ValidArrayIndex(interp, member, &error, &where);
   if (index.size() == 0 && printError) {
      const char *errorstring;
      switch (error) {
         case TMetaUtils::NOT_INT:
            errorstring = "is not an integer";
            break;
         case TMetaUtils::NOT_DEF:
            errorstring = "has not been defined before the array";
            break;
         case TMetaUtils::IS_PRIVATE:
            errorstring = "is a private member of a parent class";
            break;
         case TMetaUtils::UNKNOWN:
            errorstring = "is not known";
            break;
         default:
            errorstring = "UNKNOWN ERROR!!!!";
      }

      if (where.size() == 0) {
         ::CppyyLegacy::TMetaUtils::Error(0, "*** Datamember %s::%s: no size indication!\n",
                                 member.getParent()->getName().str().c_str(), member.getName().str().c_str());
      } else {
         ::CppyyLegacy::TMetaUtils::Error(0, "*** Datamember %s::%s: size of array (%s) %s!\n",
                                 member.getParent()->getName().str().c_str(), member.getName().str().c_str(), where.str().c_str(), errorstring);
      }
   }
   return index;
}

////////////////////////////////////////////////////////////////////////////////

void WriteStreamer(const TMetaUtils::AnnotatedRecordDecl &cl,
                   const cling::Interpreter &interp,
                   const TMetaUtils::TNormalizedCtxt &normCtxt,
                   std::ostream &dictStream)
{
   const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(cl.GetRecordDecl());
   if (clxx == 0) return;

   bool add_template_keyword = TMetaUtils::NeedTemplateKeyword(clxx);

   string fullname;
   string clsname;
   string nsname;
   int enclSpaceNesting = 0;

   if (TMetaUtils::GetNameWithinNamespace(fullname, clsname, nsname, clxx)) {
      enclSpaceNesting = TMetaUtils::WriteNamespaceHeader(dictStream, cl);
   }

   dictStream << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "void " << clsname << "::Streamer(TBuffer &R__b)"  << std::endl << "{" << std::endl
              << "   // Stream an object of class " << fullname << "." << std::endl << std::endl;

   // In case of VersionID<=0 write dummy streamer only calling
   // its base class Streamer(s). If no base class(es) let Streamer
   // print error message, i.e. this Streamer should never have been called.
   int version = TMetaUtils::GetClassVersion(clxx, interp);
   if (version <= 0) {
      // We also need to look at the base classes.
      int basestreamer = 0;
      for (clang::CXXRecordDecl::base_class_const_iterator iter = clxx->bases_begin(), end = clxx->bases_end();
            iter != end;
            ++iter) {
         if (TMetaUtils::ClassInfo__HasMethod(iter->getType()->getAsCXXRecordDecl(), "Streamer", interp)) {
            string base_fullname;
            TMetaUtils::GetQualifiedName(base_fullname, * iter->getType()->getAsCXXRecordDecl());

            if (strstr(base_fullname.c_str(), "::")) {
               // there is a namespace involved, trigger MS VC bug workaround
               dictStream << "   //This works around a msvc bug and should be harmless on other platforms" << std::endl
                          << "   typedef ::" << base_fullname << " baseClass" << basestreamer << ";" << std::endl
                          << "   baseClass" << basestreamer << "::Streamer(R__b);" << std::endl;
            } else {
               dictStream << "   ::" << base_fullname << "::Streamer(R__b);" << std::endl;
            }
            basestreamer++;
         }
      }
      if (!basestreamer) {
         dictStream << "   ::CppyyLegacy::Error(\"" << fullname << "::Streamer\", \"version id <=0 in ClassDef,"
                    " dummy Streamer() called\"); if (R__b.IsReading()) { }" << std::endl;
      }
      dictStream << "}" << std::endl << std::endl;
      while (enclSpaceNesting) {
         dictStream << "} // namespace " << nsname.c_str() << std::endl;
         --enclSpaceNesting;
      }
      return;
   }

   // loop twice: first time write reading code, second time writing code
   string classname = fullname;
   if (strstr(fullname.c_str(), "::")) {
      // there is a namespace involved, trigger MS VC bug workaround
      dictStream << "   //This works around a msvc bug and should be harmless on other platforms" << std::endl
                 << "   typedef ::" << fullname << " thisClass;" << std::endl;
      classname = "thisClass";
   }
   for (int i = 0; i < 2; i++) {

      int decli = 0;

      if (i == 0) {
         dictStream << "   UInt_t R__s, R__c;" << std::endl;
         dictStream << "   if (R__b.IsReading()) {" << std::endl;
         dictStream << "      Version_t R__v = R__b.ReadVersion(&R__s, &R__c); if (R__v) { }" << std::endl;
      } else {
         dictStream << "      R__b.CheckByteCount(R__s, R__c, " << classname.c_str() << "::IsA());" << std::endl;
         dictStream << "   } else {" << std::endl;
         dictStream << "      R__c = R__b.WriteVersion(" << classname.c_str() << "::IsA(), kTRUE);" << std::endl;
      }

      // Stream base class(es) when they have the Streamer() method
      int base = 0;
      for (clang::CXXRecordDecl::base_class_const_iterator iter = clxx->bases_begin(), end = clxx->bases_end();
            iter != end;
            ++iter) {
         if (TMetaUtils::ClassInfo__HasMethod(iter->getType()->getAsCXXRecordDecl(), "Streamer", interp)) {
            string base_fullname;
            TMetaUtils::GetQualifiedName(base_fullname, * iter->getType()->getAsCXXRecordDecl());

            if (strstr(base_fullname.c_str(), "::")) {
               // there is a namespace involved, trigger MS VC bug workaround
               dictStream << "      //This works around a msvc bug and should be harmless on other platforms" << std::endl
                          << "      typedef ::" << base_fullname << " baseClass" << base << ";" << std::endl
                          << "      baseClass" << base << "::Streamer(R__b);" << std::endl;
               ++base;
            } else {
               dictStream << "      ::" << base_fullname << "::Streamer(R__b);" << std::endl;
            }
         }
      }
      // Stream data members
      // Loop over the non static data member.
      for (clang::RecordDecl::field_iterator field_iter = clxx->field_begin(), end = clxx->field_end();
            field_iter != end;
            ++field_iter) {
         const char *comment = TMetaUtils::GetComment(**field_iter).data();

         clang::QualType type = field_iter->getType();
         std::string type_name = type.getAsString(clxx->getASTContext().getPrintingPolicy());

         const clang::Type *underling_type = TMetaUtils::GetUnderlyingType(type);

         // we skip:
         //  - static members
         //  - members with an ! as first character in the title (comment) field

         //special case for Float16_t
         int isFloat16 = 0;
         if (strstr(type_name.c_str(), "Float16_t")) isFloat16 = 1;

         //special case for Double32_t
         int isDouble32 = 0;
         if (strstr(type_name.c_str(), "Double32_t")) isDouble32 = 1;

         // No need to test for static, there are not in this list.
         if (strncmp(comment, "!", 1)) {

            // fundamental type: short, int, long, etc....
            if (underling_type->isFundamentalType() || underling_type->isEnumeralType()) {
               if (type.getTypePtr()->isConstantArrayType() &&
                     type.getTypePtr()->getArrayElementTypeNoTypeQual()->isPointerType()) {
                  const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(type.getTypePtr());
                  int s = GetFullArrayLength(arrayType);

                  if (!decli) {
                     dictStream << "      int R__i;" << std::endl;
                     decli = 1;
                  }
                  dictStream << "      for (R__i = 0; R__i < " << s << "; R__i++)" << std::endl;
                  if (i == 0) {
                     ::CppyyLegacy::TMetaUtils::Error(0, "*** Datamember %s::%s: array of pointers to fundamental type (need manual intervention)\n", fullname.c_str(), field_iter->getName().str().c_str());
                     dictStream << "         ;//R__b.ReadArray(" << field_iter->getName().str() << ");" << std::endl;
                  } else {
                     dictStream << "         ;//R__b.WriteArray(" << field_iter->getName().str() << ", __COUNTER__);" << std::endl;
                  }
               } else if (type.getTypePtr()->isPointerType()) {
                  llvm::StringRef indexvar = GrabIndex(interp, **field_iter, i == 0);
                  if (indexvar.size() == 0) {
                     if (i == 0) {
                        ::CppyyLegacy::TMetaUtils::Error(0, "*** Datamember %s::%s: pointer to fundamental type (need manual intervention)\n", fullname.c_str(), field_iter->getName().str().c_str());
                        dictStream << "      //R__b.ReadArray(" << field_iter->getName().str() << ");" << std::endl;
                     } else {
                        dictStream << "      //R__b.WriteArray(" << field_iter->getName().str() << ", __COUNTER__);" << std::endl;
                     }
                  } else {
                     if (i == 0) {
                        dictStream << "      delete [] " << field_iter->getName().str() << ";" << std::endl
                                   << "      " << GetNonConstMemberName(**field_iter) << " = new "
                                   << TMetaUtils::ShortTypeName(**field_iter) << "[" << indexvar.str() << "];" << std::endl;
                        if (isFloat16) {
                           dictStream << "      R__b.ReadFastArrayFloat16(" <<  GetNonConstMemberName(**field_iter)
                                      << "," << indexvar.str() << ");" << std::endl;
                        } else if (isDouble32) {
                           dictStream << "      R__b.ReadFastArrayDouble32(" <<  GetNonConstMemberName(**field_iter)
                                      << "," << indexvar.str() << ");" << std::endl;
                        } else {
                           dictStream << "      R__b.ReadFastArray(" << GetNonConstMemberName(**field_iter)
                                      << "," << indexvar.str() << ");" << std::endl;
                        }
                     } else {
                        if (isFloat16) {
                           dictStream << "      R__b.WriteFastArrayFloat16("
                                      << field_iter->getName().str() << "," << indexvar.str() << ");" << std::endl;
                        } else if (isDouble32) {
                           dictStream << "      R__b.WriteFastArrayDouble32("
                                      << field_iter->getName().str() << "," << indexvar.str() << ");" << std::endl;
                        } else {
                           dictStream << "      R__b.WriteFastArray("
                                      << field_iter->getName().str() << "," << indexvar.str() << ");" << std::endl;
                        }
                     }
                  }
               } else if (type.getTypePtr()->isArrayType()) {
                  if (i == 0) {
                     if (type.getTypePtr()->getArrayElementTypeNoTypeQual()->isArrayType()) { // if (m.ArrayDim() > 1) {
                        if (underling_type->isEnumeralType())
                           dictStream << "      R__b.ReadStaticArray((Int_t*)" << field_iter->getName().str() << ");" << std::endl;
                        else {
                           if (isFloat16) {
                              dictStream << "      R__b.ReadStaticArrayFloat16((" << TMetaUtils::TrueName(**field_iter)
                                         << "*)" << field_iter->getName().str() << ");" << std::endl;
                           } else if (isDouble32) {
                              dictStream << "      R__b.ReadStaticArrayDouble32((" << TMetaUtils::TrueName(**field_iter)
                                         << "*)" << field_iter->getName().str() << ");" << std::endl;
                           } else {
                              dictStream << "      R__b.ReadStaticArray((" << TMetaUtils::TrueName(**field_iter)
                                         << "*)" << field_iter->getName().str() << ");" << std::endl;
                           }
                        }
                     } else {
                        if (underling_type->isEnumeralType()) {
                           dictStream << "      R__b.ReadStaticArray((Int_t*)" << field_iter->getName().str() << ");" << std::endl;
                        } else {
                           if (isFloat16) {
                              dictStream << "      R__b.ReadStaticArrayFloat16(" << field_iter->getName().str() << ");" << std::endl;
                           } else if (isDouble32) {
                              dictStream << "      R__b.ReadStaticArrayDouble32(" << field_iter->getName().str() << ");" << std::endl;
                           } else {
                              dictStream << "      R__b.ReadStaticArray((" << TMetaUtils::TrueName(**field_iter)
                                         << "*)" << field_iter->getName().str() << ");" << std::endl;
                           }
                        }
                     }
                  } else {
                     const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(type.getTypePtr());
                     int s = GetFullArrayLength(arrayType);

                     if (type.getTypePtr()->getArrayElementTypeNoTypeQual()->isArrayType()) {// if (m.ArrayDim() > 1) {
                        if (underling_type->isEnumeralType())
                           dictStream << "      R__b.WriteArray((Int_t*)" << field_iter->getName().str() << ", "
                                      << s << ");" << std::endl;
                        else if (isFloat16) {
                           dictStream << "      R__b.WriteArrayFloat16((" << TMetaUtils::TrueName(**field_iter)
                                      << "*)" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        } else if (isDouble32) {
                           dictStream << "      R__b.WriteArrayDouble32((" << TMetaUtils::TrueName(**field_iter)
                                      << "*)" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        } else {
                           dictStream << "      R__b.WriteArray((" << TMetaUtils::TrueName(**field_iter)
                                      << "*)" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        }
                     } else {
                        if (underling_type->isEnumeralType())
                           dictStream << "      R__b.WriteArray((Int_t*)" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        else if (isFloat16) {
                           dictStream << "      R__b.WriteArrayFloat16(" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        } else if (isDouble32) {
                           dictStream << "      R__b.WriteArrayDouble32(" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        } else {
                           dictStream << "      R__b.WriteArray(" << field_iter->getName().str() << ", " << s << ");" << std::endl;
                        }
                     }
                  }
               } else if (underling_type->isEnumeralType()) {
                  if (i == 0) {
                     dictStream << "      void *ptr_" << field_iter->getName().str() << " = (void*)&" << field_iter->getName().str() << ";\n";
                     dictStream << "      R__b >> *reinterpret_cast<Int_t*>(ptr_" << field_iter->getName().str() << ");" << std::endl;
                  } else
                     dictStream << "      R__b << (Int_t)" << field_iter->getName().str() << ";" << std::endl;
               } else {
                  if (isFloat16) {
                     if (i == 0)
                        dictStream << "      {float R_Dummy; R__b >> R_Dummy; " << GetNonConstMemberName(**field_iter)
                                   << "=Float16_t(R_Dummy);}" << std::endl;
                     else
                        dictStream << "      R__b << float(" << GetNonConstMemberName(**field_iter) << ");" << std::endl;
                  } else if (isDouble32) {
                     if (i == 0)
                        dictStream << "      {float R_Dummy; R__b >> R_Dummy; " << GetNonConstMemberName(**field_iter)
                                   << "=Double32_t(R_Dummy);}" << std::endl;
                     else
                        dictStream << "      R__b << float(" << GetNonConstMemberName(**field_iter) << ");" << std::endl;
                  } else {
                     if (i == 0)
                        dictStream << "      R__b >> " << GetNonConstMemberName(**field_iter) << ";" << std::endl;
                     else
                        dictStream << "      R__b << " << GetNonConstMemberName(**field_iter) << ";" << std::endl;
                  }
               }
            } else {
               // we have an object...

               // check if object is a standard string
               if (STLStringStreamer(**field_iter, i, dictStream))
                  continue;

               // check if object is an STL container
               if (STLContainerStreamer(**field_iter, i, interp, normCtxt, dictStream))
                  continue;

               // handle any other type of objects
               if (type.getTypePtr()->isConstantArrayType() &&
                     type.getTypePtr()->getArrayElementTypeNoTypeQual()->isPointerType()) {
                  const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(type.getTypePtr());
                  int s = GetFullArrayLength(arrayType);

                  if (!decli) {
                     dictStream << "      int R__i;" << std::endl;
                     decli = 1;
                  }
                  dictStream << "      for (R__i = 0; R__i < " << s << "; R__i++)" << std::endl;
                  if (i == 0)
                     dictStream << "         R__b >> " << GetNonConstMemberName(**field_iter);
                  else {
                     if (TMetaUtils::IsBase(**field_iter, "CppyyLegacy::TObject", interp) && TMetaUtils::IsBase(**field_iter, "CppyyLegacy::TArray", interp))
                        dictStream << "         R__b << (::CppyyLegacy::TObject*)" << field_iter->getName().str();
                     else
                        dictStream << "         R__b << " << GetNonConstMemberName(**field_iter);
                  }
                  WriteArrayDimensions(field_iter->getType(), dictStream);
                  dictStream << "[R__i];" << std::endl;
               } else if (type.getTypePtr()->isPointerType()) {
                  // This is always good. However, in case of a pointer
                  // to an object that is guaranteed to be there and not
                  // being referenced by other objects we could use
                  //     xx->Streamer(b);
                  // Optimize this with control statement in title.
                  if (isPointerToPointer(**field_iter)) {
                     if (i == 0) {
                        ::CppyyLegacy::TMetaUtils::Error(0, "*** Datamember %s::%s: pointer to pointer (need manual intervention)\n", fullname.c_str(), field_iter->getName().str().c_str());
                        dictStream << "      //R__b.ReadArray(" << field_iter->getName().str() << ");" << std::endl;
                     } else {
                        dictStream << "      //R__b.WriteArray(" << field_iter->getName().str() << ", __COUNTER__);";
                     }
                  } else {
                     if (i == 0) {
                        // The following:
                        //    if (strncmp(m.Title(),"->",2) != 0) fprintf(fp, "      delete %s;\n", GetNonConstMemberName(**field_iter).c_str());
                        // could be used to prevent a memory leak since the next statement could possibly create a new object.
                        // In the TStreamerInfo based I/O we made the previous statement conditional on TStreamerInfo::CanDelete
                        // to allow the user to prevent some inadvisable deletions.  So we should be offering this flexibility
                        // here to and should not (technically) rely on TStreamerInfo for it, so for now we leave it as is.
                        // Note that the leak should happen from here only if the object is stored in an unsplit object
                        // and either the user request an old branch or the streamer has been customized.
                        dictStream << "      R__b >> " << GetNonConstMemberName(**field_iter) << ";" << std::endl;
                     } else {
                        if (TMetaUtils::IsBase(**field_iter, "CppyyLegacy::TObject", interp) && TMetaUtils::IsBase(**field_iter, "CppyyLegacy::TArray", interp))
                           dictStream << "      R__b << (::CppyyLegacy::TObject*)" << field_iter->getName().str() << ";" << std::endl;
                        else
                           dictStream << "      R__b << " << GetNonConstMemberName(**field_iter) << ";" << std::endl;
                     }
                  }
               } else if (const clang::ConstantArrayType *arrayType = llvm::dyn_cast<clang::ConstantArrayType>(type.getTypePtr())) {
                  int s = GetFullArrayLength(arrayType);

                  if (!decli) {
                     dictStream << "      int R__i;" << std::endl;
                     decli = 1;
                  }
                  dictStream << "      for (R__i = 0; R__i < " << s << "; R__i++)" << std::endl;
                  std::string mTypeNameStr;
                  TMetaUtils::GetQualifiedName(mTypeNameStr, field_iter->getType(), **field_iter);
                  const char *mTypeName = mTypeNameStr.c_str();
                  const char *constwd = "const ";
                  if (strncmp(constwd, mTypeName, strlen(constwd)) == 0) {
                     mTypeName += strlen(constwd);
                     dictStream << "         const_cast< " << mTypeName << " &>(" << field_iter->getName().str();
                     WriteArrayDimensions(field_iter->getType(), dictStream);
                     dictStream << "[R__i]).Streamer(R__b);" << std::endl;
                  } else {
                     dictStream << "         " << GetNonConstMemberName(**field_iter);
                     WriteArrayDimensions(field_iter->getType(), dictStream);
                     dictStream << "[R__i].Streamer(R__b);" << std::endl;
                  }
               } else {
                  if (TMetaUtils::ClassInfo__HasMethod(TMetaUtils::GetUnderlyingRecordDecl(field_iter->getType()), "Streamer", interp))
                     dictStream << "      " << GetNonConstMemberName(**field_iter) << ".Streamer(R__b);" << std::endl;
                  else {
                     dictStream << "      R__b.StreamObject(&(" << field_iter->getName().str() << "),typeid("
                                << field_iter->getName().str() << "));" << std::endl;               //R__t.Streamer(R__b);\n");
                     //VP                     if (i == 0)
                     //VP                        Error(0, "*** Datamember %s::%s: object has no Streamer() method (need manual intervention)\n",
                     //VP                                  fullname, field_iter->getName().str());
                     //VP                     fprintf(fp, "      //%s.Streamer(R__b);\n", m.Name());
                  }
               }
            }
         }
      }
   }
   dictStream << "      R__b.SetByteCount(R__c, kTRUE);" << std::endl
              << "   }" << std::endl
              << "}" << std::endl << std::endl;

   while (enclSpaceNesting) {
      dictStream << "} // namespace " << nsname.c_str() << std::endl;
      --enclSpaceNesting;
   }
}

////////////////////////////////////////////////////////////////////////////////

void WriteAutoStreamer(const TMetaUtils::AnnotatedRecordDecl &cl,
                       const cling::Interpreter &interp,
                       const TMetaUtils::TNormalizedCtxt &normCtxt,
                       std::ostream &dictStream)
{
   // Write Streamer() method suitable for automatic schema evolution.

   const clang::CXXRecordDecl *clxx = llvm::dyn_cast<clang::CXXRecordDecl>(cl.GetRecordDecl());
   if (clxx == 0) return;

   bool add_template_keyword = TMetaUtils::NeedTemplateKeyword(clxx);

   // We also need to look at the base classes.
   for (clang::CXXRecordDecl::base_class_const_iterator iter = clxx->bases_begin(), end = clxx->bases_end();
         iter != end;
         ++iter) {
      int k = TMetaUtils::IsSTLContainer(*iter);
      if (k != 0) {
         Internal::RStl::Instance().GenerateTClassFor(iter->getType(), interp, normCtxt);
      }
   }

   string fullname;
   string clsname;
   string nsname;
   int enclSpaceNesting = 0;

   if (TMetaUtils::GetNameWithinNamespace(fullname, clsname, nsname, clxx)) {
      enclSpaceNesting = TMetaUtils::WriteNamespaceHeader(dictStream, cl);
   }

   dictStream << "//_______________________________________"
              << "_______________________________________" << std::endl;
   if (add_template_keyword) dictStream << "template <> ";
   dictStream << "void " << clsname << "::Streamer(TBuffer &R__b)" << std::endl
              << "{" << std::endl
              << "   // Stream an object of class " << fullname << "." << std::endl << std::endl
              << "   if (R__b.IsReading()) {" << std::endl
              << "      R__b.ReadClassBuffer(::" << fullname << "::Class(),this);" << std::endl
              << "   } else {" << std::endl
              << "      R__b.WriteClassBuffer(::" << fullname << "::Class(),this);" << std::endl
              << "   }" << std::endl
              << "}" << std::endl << std::endl;

   while (enclSpaceNesting) {
      dictStream << "} // namespace " << nsname << std::endl;
      --enclSpaceNesting;
   }
}

////////////////////////////////////////////////////////////////////////////////

void CallWriteStreamer(const TMetaUtils::AnnotatedRecordDecl &cl,
                       const cling::Interpreter &interp,
                       const TMetaUtils::TNormalizedCtxt &normCtxt,
                       std::ostream &dictStream,
                       bool isAutoStreamer)
{
   if (isAutoStreamer) {
      WriteAutoStreamer(cl, interp, normCtxt, dictStream);
   } else {
      WriteStreamer(cl, interp, normCtxt, dictStream);
   }
}

////////////////////////////////////////////////////////////////////////////////

void GenerateLinkdef(llvm::cl::list<std::string> &InputFiles,
                     std::string &code_for_parser)
{
   code_for_parser += "#ifdef __CINT__\n\n";
   code_for_parser += "#pragma link off all globals;\n";
   code_for_parser += "#pragma link off all classes;\n";
   code_for_parser += "#pragma link off all functions;\n\n";

   for (std::string& arg : InputFiles) {
      char trail[3];
      int nostr = 0, noinp = 0, bcnt = 0, l = arg.length() - 1;
      for (int j = 0; j < 3; j++) {
         if (arg[l] == '-') {
            arg[l] = '\0';
            nostr = 1;
            l--;
         }
         if (arg[l] == '!') {
            arg[l] = '\0';
            noinp = 1;
            l--;
         }
         if (arg[l] == '+') {
            arg[l] = '\0';
            bcnt = 1;
            l--;
         }
      }
      if (nostr || noinp) {
         trail[0] = 0;
         if (nostr) strlcat(trail, "-", 3);
         if (noinp) strlcat(trail, "!", 3);
      }
      if (bcnt) {
         strlcpy(trail, "+", 3);
         if (nostr)
            ::CppyyLegacy::TMetaUtils::Error(0, "option + mutual exclusive with -\n");
      }
      llvm::SmallString<256> filestem = llvm::sys::path::filename(arg);
      llvm::sys::path::replace_extension(filestem, "");

      code_for_parser += "#pragma link C++ class ";
      code_for_parser += filestem.str().str();
      if (nostr || noinp || bcnt)
         code_for_parser += trail;
      code_for_parser += ";\n";
   }

   code_for_parser += "\n#endif\n";
}

////////////////////////////////////////////////////////////////////////////////
/// Find file name in path specified via -I statements to Cling.
/// Return false if the file can not be found.
/// If the file is found, set pname to the full path name and return true.

bool Which(cling::Interpreter &interp, const char *fname, string &pname)
{
   FILE *fp = 0;

#ifdef WIN32
   static const char *fopenopts = "rb";
#else
   static const char *fopenopts = "r";
#endif

   pname = fname;
   fp = fopen(pname.c_str(), fopenopts);
   if (fp) {
      fclose(fp);
      return true;
   }

   llvm::SmallVector<std::string, 10> includePaths;//Why 10? Hell if I know.
   //false - no system header, false - with flags.
   interp.GetIncludePaths(includePaths, false, false);

   const size_t nPaths = includePaths.size();
   for (size_t i = 0; i < nPaths; i += 1 /* 2 */) {

      pname = includePaths[i].c_str() + gPathSeparator + fname;

      fp = fopen(pname.c_str(), fopenopts);
      if (fp) {
         fclose(fp);
         return true;
      }
   }
   pname = "";
   return false;
}

////////////////////////////////////////////////////////////////////////////////
/// If the argument starts with MODULE/inc, strip it
/// to make it the name we can use in #includes.

const char *CopyArg(const char *original)
{
   if (!gBuildingROOT)
      return original;

   if (IsSelectionFile(original))
      return original;

   const char *inc = strstr(original, "\\inc\\");
   if (!inc)
      inc = strstr(original, "/inc/");
   if (inc && strlen(inc) > 5)
      return inc + 5;
   return original;
}

////////////////////////////////////////////////////////////////////////////////
/// Copy the command line argument, stripping MODULE/inc if
/// necessary.

void StrcpyArg(string &dest, const char *original)
{
   dest = CopyArg(original);
}

////////////////////////////////////////////////////////////////////////////////
/// Write the extra header injected into the module:
/// umbrella header if (umbrella) else content header.

static bool InjectModuleUtilHeader(const char *argv0,
                                   TModuleGenerator &modGen,
                                   cling::Interpreter &interp,
                                   bool umbrella)
{
   std::ostringstream out;
   if (umbrella) {
      // This will duplicate the -D,-U from clingArgs - but as they are surrounded
      // by #ifndef there is no problem here.
      modGen.WriteUmbrellaHeader(out);
   } else {
      modGen.WriteContentHeader(out);
   }
   if (interp.declare(out.str()) != cling::Interpreter::kSuccess) {
      const std::string &hdrName
         = umbrella ? modGen.GetUmbrellaName() : modGen.GetContentName();
      ::CppyyLegacy::TMetaUtils::Error(0, "%s: compilation failure (%s)\n", argv0,
                              hdrName.c_str());
      return false;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
/// Write the AST of the given CompilerInstance to the given File while
/// respecting the given isysroot.
/// If module is not a null pointer, we only write the given module to the
/// given file and not the whole AST.
/// Returns true if the AST was successfully written.
static bool WriteAST(llvm::StringRef fileName, clang::CompilerInstance *compilerInstance,
                     llvm::StringRef iSysRoot,
                     clang::Module *module = nullptr)
{
   // From PCHGenerator and friends:
   llvm::SmallVector<char, 128> buffer;
   llvm::BitstreamWriter stream(buffer);
   clang::ASTWriter writer(stream, buffer, compilerInstance->getModuleCache(), /*Extensions=*/{});
   std::unique_ptr<llvm::raw_ostream> out =
      compilerInstance->createOutputFile(fileName, /*Binary=*/true,
                                         /*RemoveFileOnSignal=*/false,
                                         /*useTemporary=*/false,
                                         /*CreateMissingDirectories*/ false);
   if (!out) {
      ::CppyyLegacy::TMetaUtils::Error("WriteAST", "Couldn't open output stream to '%s'!\n", fileName.data());
      return false;
   }

   compilerInstance->getFrontendOpts().RelocatablePCH = true;

   writer.WriteAST(compilerInstance->getSema(), fileName.str(), module, iSysRoot);

   // Write the generated bitstream to "Out".
   out->write(&buffer.front(), buffer.size());

   // Make sure it hits disk now.
   out->flush();

   return true;
}

////////////////////////////////////////////////////////////////////////////////
/// Generates a PCH from the given ModuleGenerator and CompilerInstance.
/// Returns true iff the PCH was successfully generated.
static bool GenerateAllDict(TModuleGenerator &modGen, clang::CompilerInstance *compilerInstance,
                            const std::string &currentDirectory)
{
   assert(modGen.IsPCH() && "modGen must be in PCH mode");

   std::string iSysRoot("/DUMMY_SYSROOT/include/");
   if (gBuildingROOT) iSysRoot = (currentDirectory + "/");
   return WriteAST(modGen.GetModuleFileName(), compilerInstance, iSysRoot);
}

////////////////////////////////////////////////////////////////////////////////
/// Includes all given headers in the interpreter. Returns true when we could
/// include the headers and otherwise false on an error when including.
static bool IncludeHeaders(const std::vector<std::string> &headers, cling::Interpreter &interpreter)
{
   // If no headers are given, this is a no-op.
   if (headers.empty())
      return true;

   // Turn every header name into an include and parse it in the interpreter.
   std::stringstream includes;
   for (const std::string &header : headers) {
      includes << "#include \"" << header << "\"\n";
   }
   std::string includeListStr = includes.str();
   auto result = interpreter.declare(includeListStr);
   return result == cling::Interpreter::CompilationResult::kSuccess;
}


////////////////////////////////////////////////////////////////////////////////

void AddPlatformDefines(std::vector<std::string> &clingArgs)
{
   char platformDefines[64] = {0};
#ifdef __INTEL_COMPILER
   snprintf(platformDefines, 64, "-DG__INTEL_COMPILER=%ld", (long)__INTEL_COMPILER);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __xlC__
   snprintf(platformDefines, 64, "-DG__xlC=%ld", (long)__xlC__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __GNUC__
   snprintf(platformDefines, 64, "-DG__GNUC=%ld", (long)__GNUC__);
   snprintf(platformDefines, 64, "-DG__GNUC_VER=%ld", (long)__GNUC__ * 1000 + __GNUC_MINOR__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __GNUC_MINOR__
   snprintf(platformDefines, 64, "-DG__GNUC_MINOR=%ld", (long)__GNUC_MINOR__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __HP_aCC
   snprintf(platformDefines, 64, "-DG__HP_aCC=%ld", (long)__HP_aCC);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __sun
   snprintf(platformDefines, 64, "-DG__sun=%ld", (long)__sun);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __SUNPRO_CC
   snprintf(platformDefines, 64, "-DG__SUNPRO_CC=%ld", (long)__SUNPRO_CC);
   clingArgs.push_back(platformDefines);
#endif
#ifdef _STLPORT_VERSION
   // stlport version, used on e.g. SUN
   snprintf(platformDefines, 64, "-DG__STLPORT_VERSION=%ld", (long)_STLPORT_VERSION);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __ia64__
   snprintf(platformDefines, 64, "-DG__ia64=%ld", (long)__ia64__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __x86_64__
   snprintf(platformDefines, 64, "-DG__x86_64=%ld", (long)__x86_64__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __i386__
   snprintf(platformDefines, 64, "-DG__i386=%ld", (long)__i386__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef __arm__
   snprintf(platformDefines, 64, "-DG__arm=%ld", (long)__arm__);
   clingArgs.push_back(platformDefines);
#endif
#ifdef _WIN32
   snprintf(platformDefines, 64, "-DG__WIN32=%ld", (long)_WIN32);
   clingArgs.push_back(platformDefines);
#else
# ifdef WIN32
   snprintf(platformDefines, 64, "-DG__WIN32=%ld", (long)WIN32);
   clingArgs.push_back(platformDefines);
# endif
#endif
#ifdef _MSC_VER
   snprintf(platformDefines, 64, "-DG__MSC_VER=%ld", (long)_MSC_VER);
   clingArgs.push_back(platformDefines);
   snprintf(platformDefines, 64, "-DG__VISUAL=%ld", (long)_MSC_VER);
   clingArgs.push_back(platformDefines);
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Extract the filename from a fullpath

std::string ExtractFileName(const std::string &path)
{
   return llvm::sys::path::filename(path).str();
}

////////////////////////////////////////////////////////////////////////////////
/// Extract the path from a fullpath finding the last \ or /
/// according to the content in gPathSeparator

void ExtractFilePath(const std::string &path, std::string &dirname)
{
   const size_t pos = path.find_last_of(gPathSeparator);
   if (std::string::npos != pos) {
      dirname.assign(path.begin(), path.begin() + pos + 1);
   } else {
      dirname.assign("");
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Check if file has a path

bool HasPath(const std::string &name)
{
   std::string dictLocation;
   ExtractFilePath(name, dictLocation);
   return !dictLocation.empty();
}

////////////////////////////////////////////////////////////////////////////////

void AdjustRootMapNames(std::string &rootmapFileName,
                        std::string &rootmapLibName)
{
   // If the rootmap file name does not exist, create one following the libname
   // I.E. put into the directory of the lib the rootmap and within the rootmap the normalised path to the lib
   if (rootmapFileName.empty()) {
      size_t libExtensionPos = rootmapLibName.find_last_of(gLibraryExtension) - gLibraryExtension.size() + 1;
      rootmapFileName = rootmapLibName.substr(0, libExtensionPos) + ".rootmap";
      size_t libCleanNamePos = rootmapLibName.find_last_of(gPathSeparator) + 1;
      rootmapLibName = rootmapLibName.substr(libCleanNamePos, std::string::npos);
      ::CppyyLegacy::TMetaUtils::Info(0, "Rootmap file name %s built from rootmap lib name %s",
                             rootmapLibName.c_str(),
                             rootmapFileName.c_str());
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Extract the proper autoload key for nested classes
/// The routine does not erase the name, just updates it

void GetMostExternalEnclosingClassName(const clang::DeclContext &theContext,
                                       std::string &ctxtName,
                                       const cling::Interpreter &interpreter,
                                       bool treatParent = true)
{
   const clang::DeclContext *outerCtxt = treatParent ? theContext.getParent() : &theContext;
   // If the context has no outer context, we are finished
   if (!outerCtxt) return;
   // If the context is a class, we update the name
   if (const clang::RecordDecl *thisRcdDecl = llvm::dyn_cast<clang::RecordDecl>(outerCtxt)) {
      TMetaUtils::GetNormalizedName(ctxtName, thisRcdDecl, interpreter);
   }
   // We recurse
   GetMostExternalEnclosingClassName(*outerCtxt, ctxtName, interpreter);
}

////////////////////////////////////////////////////////////////////////////////

void GetMostExternalEnclosingClassNameFromDecl(const clang::Decl &theDecl,
      std::string &ctxtName,
      const cling::Interpreter &interpreter)
{
   const clang::DeclContext *theContext = theDecl.getDeclContext();
   GetMostExternalEnclosingClassName(*theContext, ctxtName, interpreter, false);
}

////////////////////////////////////////////////////////////////////////////////
template<class COLL>
int ExtractAutoloadKeys(std::list<std::string> &names,
                        const COLL &decls,
                        const cling::Interpreter &interp)
{
   if (!decls.empty()) {
      std::string autoLoadKey;
      for (auto & d : decls) {
         autoLoadKey = "";
         GetMostExternalEnclosingClassNameFromDecl(*d, autoLoadKey, interp);
         // If there is an outer class, it is already considered
         if (autoLoadKey.empty()) {
            names.push_back(d->getQualifiedNameAsString());
         }
      }
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Generate a rootmap file in the new format, like
/// { decls }
/// namespace A { namespace B { template <typename T> class myTemplate; } }
/// [libGpad.so libGraf.so libHist.so libMathCore.so]
/// class TAttCanvas
/// class TButton
/// (header1.h header2.h .. headerN.h)
/// class TMyClass

int CreateNewRootMapFile(const std::string &rootmapFileName,
                         const std::string &rootmapLibName,
                         const std::list<std::string> &classesDefsList,
                         const std::list<std::string> &classesNames,
                         const std::list<std::string> &nsNames,
                         const std::list<std::string> &tdNames,
                         const std::list<std::string> &enNames,
                         const std::list<std::string> &varNames,
                         const HeadersDeclsMap_t &headersClassesMap,
                         const std::unordered_set<std::string> headersToIgnore)
{
   // Create the rootmap file from the selected classes and namespaces
   std::ofstream rootmapFile(rootmapFileName.c_str());
   if (!rootmapFile) {
      ::CppyyLegacy::TMetaUtils::Error(0, "Opening new rootmap file %s\n", rootmapFileName.c_str());
      return 1;
   }

   // Keep track of the classes keys
   // This is done to avoid duplications of keys with typedefs
   std::unordered_set<std::string> classesKeys;


   // Add the "section"
   if (!classesNames.empty() || !nsNames.empty() || !tdNames.empty() ||
      !enNames.empty() || !varNames.empty()) {

      // Add the template definitions
      if (!classesDefsList.empty()) {
         rootmapFile << "{ decls }\n";
         for (auto & classDef : classesDefsList) {
            rootmapFile << classDef << std::endl;
         }
         rootmapFile << "\n";
      }
      rootmapFile << "[ " << rootmapLibName << " ]\n";

      // Loop on selected classes and insert them in the rootmap
      if (!classesNames.empty()) {
         rootmapFile << "# List of selected classes\n";
         for (auto & className : classesNames) {
            rootmapFile << "class " << className << std::endl;
            classesKeys.insert(className);
         }
         // And headers
         std::unordered_set<std::string> treatedHeaders;
         for (auto & className : classesNames) {
            // Don't treat templates
            if (className.find("<") != std::string::npos) continue;
            if (headersClassesMap.count(className)) {
               auto &headers = headersClassesMap.at(className);
               if (!headers.empty()){
                  auto &header = headers.front();
                  if (treatedHeaders.insert(header).second &&
                        headersToIgnore.find(header) == headersToIgnore.end() &&
                        TMetaUtils::IsHeaderName(header)){
                        rootmapFile << "header " << header << std::endl;
                  }
               }
            }
         }
      }

      // Same for namespaces
      if (!nsNames.empty()) {
         rootmapFile << "# List of selected namespaces\n";
         for (auto & nsName : nsNames) {
            rootmapFile << "namespace " << nsName << std::endl;
         }
      }

      // And typedefs. These are used just to trigger the autoload mechanism
      if (!tdNames.empty()) {
         rootmapFile << "# List of selected typedefs and outer classes\n";
         for (const auto & autoloadKey : tdNames)
            if (classesKeys.insert(autoloadKey).second)
               rootmapFile << "typedef " << autoloadKey << std::endl;
      }

      // And Enums. There is no incomplete type for an enum but we can nevertheless
      // have the key for the cases where the root typesystem is interrogated.
      if (!enNames.empty()){
         rootmapFile << "# List of selected enums and outer classes\n";
         for (const auto & autoloadKey : enNames)
            if (classesKeys.insert(autoloadKey).second)
               rootmapFile << "enum " << autoloadKey << std::endl;
      }

      // And variables.
      if (!varNames.empty()){
         rootmapFile << "# List of selected vars\n";
         for (const auto & autoloadKey : varNames)
            if (classesKeys.insert(autoloadKey).second)
               rootmapFile << "var " << autoloadKey << std::endl;
      }

   }

   return 0;

}

////////////////////////////////////////////////////////////////////////////////
/// Performance is not critical here.

std::pair<std::string,std::string> GetExternalNamespaceAndContainedEntities(const std::string line)
{
   auto nsPattern = '{'; auto nsPatternLength = 1;
   auto foundNsPos = line.find_last_of(nsPattern);
   if (foundNsPos == std::string::npos) return {"",""};
   foundNsPos+=nsPatternLength;
   auto extNs = line.substr(0,foundNsPos);

   auto nsEndPattern = '}';
   auto foundEndNsPos = line.find(nsEndPattern);
   auto contained = line.substr(foundNsPos, foundEndNsPos-foundNsPos);

   return {extNs, contained};


}

////////////////////////////////////////////////////////////////////////////////
/// If two identical namespaces are there, just declare one only
/// Example:
/// namespace A { namespace B { fwd1; }}
/// namespace A { namespace B { fwd2; }}
/// get a namespace A { namespace B { fwd1; fwd2; }} line

std::list<std::string> CollapseIdenticalNamespaces(const std::list<std::string>& fwdDeclarationsList)
{
   // Temp data structure holding the namespaces and the entities therewith
   // contained
   std::map<std::string, std::string> nsEntitiesMap;
   std::list<std::string> optFwdDeclList;
   for (auto const & fwdDecl : fwdDeclarationsList){
      // Check if the decl(s) are contained in a ns and which one
      auto extNsAndEntities = GetExternalNamespaceAndContainedEntities(fwdDecl);
      if (extNsAndEntities.first.empty()) {
         // no namespace found. Just put this on top
         optFwdDeclList.push_front(fwdDecl);
      };
      auto currentVal = nsEntitiesMap[extNsAndEntities.first];
      nsEntitiesMap[extNsAndEntities.first] = currentVal +=extNsAndEntities.second;
   }

   // Now fill the new, optimised list
   std::string optFwdDecl;
   for (auto const & extNsAndEntities : nsEntitiesMap) {
      optFwdDecl = extNsAndEntities.first;
      optFwdDecl += extNsAndEntities.second;
      for (int i = 0; i < std::count(optFwdDecl.begin(), optFwdDecl.end(), '{'); ++i ){
         optFwdDecl += " }";
      }
      optFwdDeclList.push_front(optFwdDecl);
   }

   return optFwdDeclList;

}

////////////////////////////////////////////////////////////////////////////////
/// Separate multiline strings

bool ProcessAndAppendIfNotThere(const std::string &el,
                                std::list<std::string> &el_list,
                                std::unordered_set<std::string> &el_set)
{
   std::stringstream elStream(el);
   std::string tmp;
   bool added = false;
   while (getline(elStream, tmp, '\n')) {
      // Add if not there
      if (el_set.insert(tmp).second && !tmp.empty()) {
         el_list.push_back(tmp);
         added = true;
      }
   }

   return added;
}

////////////////////////////////////////////////////////////////////////////////

static int TreatSingleTemplateArg(const clang::TemplateArgument& arg,
                                  std::string& argFwdDecl,
                                  const  cling::Interpreter& interpreter,
                                  bool acceptStl=false)
{
   using namespace CppyyLegacy::TMetaUtils::AST2SourceTools;

   // We do nothing in presence of ints, bools, templates.
   // We should probably in presence of templates though...
   if (clang::TemplateArgument::Type != arg.getKind()) return 0;

   auto argQualType = arg.getAsType();

   // Recursively remove all *
   while (llvm::isa<clang::PointerType>(argQualType.getTypePtr())) argQualType = argQualType->getPointeeType();

   auto argTypePtr = argQualType.getTypePtr();

   // Bail out on enums
   if (llvm::isa<clang::EnumType>(argTypePtr)){
      return 1;
   }

   // If this is a built-in, just return: fwd decl not necessary.
   if (llvm::isa<clang::BuiltinType>(argTypePtr)){
      return 0;
   }

   // Treat typedefs which are arguments
   if (auto tdTypePtr = llvm::dyn_cast<clang::TypedefType>(argTypePtr)) {
      FwdDeclFromTypeDefNameDecl(*tdTypePtr->getDecl(), interpreter, argFwdDecl);
      return 0;
   }

   if (auto argRecTypePtr = llvm::dyn_cast<clang::RecordType>(argTypePtr)){
      // Now we cannot but have a RecordType
      if (auto argRecDeclPtr = argRecTypePtr->getDecl()){
         FwdDeclFromRcdDecl(*argRecDeclPtr,interpreter,argFwdDecl,acceptStl);
      }
      return 0;
   }

   return 1;
}

////////////////////////////////////////////////////////////////////////////////
/// Convert a tmplt decl to its fwd decl

static int FwdDeclIfTmplSpec(const clang::RecordDecl& recordDecl,
                             const cling::Interpreter& interpreter,
                             std::string& defString,
                             const std::string &normalizedName)
{
   // If this is an explicit specialization, inject it into cling, too, such that it can have
   // externalLexicalStorage, see TCling.cxx's ExtVisibleStorageAdder::VisitClassTemplateSpecializationDecl.
   if (auto tmplSpecDeclPtr = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&recordDecl)) {
      if (const auto *specDef = tmplSpecDeclPtr->getDefinition()) {
         if (specDef->getTemplateSpecializationKind() != clang::TSK_ExplicitSpecialization)
            return 0;
         // normalizedName contains scope, no need to enclose in namespace!
         for (auto arg : tmplSpecDeclPtr->getTemplateArgs().asArray()) {
            std::string argFwdDecl;
            int retCode = TreatSingleTemplateArg(arg, argFwdDecl, interpreter, /*acceptStl=*/false);
            if (retCode != 0) { // A sign we must bail out
               return retCode;
            }
            defString += argFwdDecl + '\n';
         }
         defString += "template <> class " + normalizedName + ';';
         return 0;
      }
   }

   return 0;
}

////////////////////////////////////////////////////////////////////////////////

int  ExtractClassesListAndDeclLines(RScanner &scan,
      std::list<std::string> &classesList,
      std::list<std::string> &classesListForRootmap,
      std::list<std::string> &fwdDeclarationsList,
      const cling::Interpreter &interpreter)
{
   // Loop on selected classes. If they don't have the attribute "rootmap"
   // set to "false", store them in the list of classes for the rootmap
   // Returns 0 in case of success and 1 in case of issues.

   // An unordered_set to keep track of the existing classes.
   // We want to avoid duplicates there as they may hint to a serious corruption
   std::unordered_set<std::string> classesSet;
   std::unordered_set<std::string> outerMostClassesSet;

   std::string attrName, attrValue;
   bool isClassSelected;
   std::unordered_set<std::string> availableFwdDecls;
   std::string fwdDeclaration;
   for (auto const & selVar : scan.fSelectedVariables) {
      fwdDeclaration = "";
      int retCode = TMetaUtils::AST2SourceTools::EncloseInNamespaces(*selVar, fwdDeclaration);
      if (retCode == 0) ProcessAndAppendIfNotThere(fwdDeclaration, fwdDeclarationsList, availableFwdDecls);
   }

   for (auto const & selEnum : scan.fSelectedEnums) {
      fwdDeclaration = "";
      int retCode = TMetaUtils::AST2SourceTools::EncloseInNamespaces(*selEnum, fwdDeclaration);
      if (retCode == 0) ProcessAndAppendIfNotThere(fwdDeclaration, fwdDeclarationsList, availableFwdDecls);
   }

   // Loop on selected classes and put them in a list
   for (auto const & selClass : scan.fSelectedClasses) {
      isClassSelected = true;
      const clang::RecordDecl *rDecl = selClass.GetRecordDecl();
      std::string normalizedName;
      normalizedName = selClass.GetNormalizedName();
      if (!normalizedName.empty() &&
            !classesSet.insert(normalizedName).second &&
            outerMostClassesSet.count(normalizedName) == 0) {
         std::cerr << "FATAL: A class with normalized name " << normalizedName
                   << " was already selected. This means that two different instances of"
                   << " clang::RecordDecl had the same name, which is not possible."
                   << " This can be a hint of a serious problem in the class selection."
                   << " In addition, the generated dictionary would not even compile.\n";
         return 1;
      }
      classesList.push_back(normalizedName);
      // Allow to autoload with the name of the class as it was specified in the
      // selection xml or linkdef
      const char *reqName(selClass.GetRequestedName());

      // Get always the containing namespace, put it in the list if not there
      fwdDeclaration = "";
      int retCode = TMetaUtils::AST2SourceTools::EncloseInNamespaces(*rDecl, fwdDeclaration);
      if (retCode == 0) ProcessAndAppendIfNotThere(fwdDeclaration, fwdDeclarationsList, availableFwdDecls);

      // Get template definition and put it in if not there
      if (llvm::isa<clang::ClassTemplateSpecializationDecl>(rDecl)) {
         fwdDeclaration = "";
         retCode = CppyyLegacy::TMetaUtils::AST2SourceTools::FwdDeclFromRcdDecl(*rDecl, interpreter, fwdDeclaration);
         if (retCode == 0) {
            std::string fwdDeclarationTemplateSpec;
            retCode = FwdDeclIfTmplSpec(*rDecl, interpreter, fwdDeclarationTemplateSpec, normalizedName);
            fwdDeclaration += '\n' + fwdDeclarationTemplateSpec;
         }
         if (retCode == 0)
            ProcessAndAppendIfNotThere(fwdDeclaration, fwdDeclarationsList, availableFwdDecls);
      }

      // Loop on attributes, if rootmap=false, don't put it in the list!
      for (auto ait = rDecl->attr_begin(); ait != rDecl->attr_end(); ++ait) {
         if (0 == TMetaUtils::extractPropertyNameVal(*ait, attrName, attrValue) &&
               attrName == "rootmap" &&
               attrValue == "false") {
            attrName = attrValue = "";
            isClassSelected = false;
            break;
         }
      }
      if (isClassSelected) {
         // Now, check if this is an internal class. If yes, we check the name of the outermost one
         // This is because of ROOT-6517. On the other hand, we exclude from this treatment
         // classes which are template instances which are nested in classes. For example:
         // class A{
         //   class B{};
         // };
         // selection: <class name="A::B" />
         // Will result in a rootmap entry like "class A"
         // On the other hand, taking
         // class A{
         //    public:
         //     template <class T> class B{};
         //  };
         // selection: <class name="A::B<int>" />
         // Would result in an entry like "class A::B<int>"
         std::string outerMostClassName;
         GetMostExternalEnclosingClassName(*rDecl, outerMostClassName, interpreter);
         if (!outerMostClassName.empty() &&
             !llvm::isa<clang::ClassTemplateSpecializationDecl>(rDecl) &&
             classesSet.insert(outerMostClassName).second &&
             outerMostClassesSet.insert(outerMostClassName).second) {
            if (outerMostClassName.find("Long64_t") == std::string::npos)
                classesListForRootmap.push_back(outerMostClassName);
         } else {
            if (normalizedName.find("Long64_t") == std::string::npos)
               classesListForRootmap.push_back(normalizedName);
            if (reqName != nullptr && 0 != strcmp(reqName, "") && reqName != normalizedName) {
               if (!strstr(reqName, "Long64_t"))
                  classesListForRootmap.push_back(reqName);
            }

            // Also register typeinfo::name(), unless we have pseudo-strong typedefs:
            if (normalizedName.find("Double32_t") == std::string::npos
                && normalizedName.find("Float16_t") == std::string::npos) {
               std::unique_ptr<clang::MangleContext> mangleCtx(rDecl->getASTContext().createMangleContext());
               std::string mangledName;
               {
                  llvm::raw_string_ostream sstr(mangledName);
                  if (const clang::TypeDecl* TD = llvm::dyn_cast<clang::TypeDecl>(rDecl)) {
                     mangleCtx->mangleCXXRTTI(clang::QualType(TD->getTypeForDecl(), 0), sstr);
                  }
               }
               if (!mangledName.empty()) {
                  int errDemangle = 0;
#ifdef WIN32
                  if (mangledName[0] == '\01')
                     mangledName.erase(0, 1);
                  char *demangledTIName = TClassEdit::DemangleName(mangledName.c_str(), errDemangle);
                  if (!errDemangle && demangledTIName) {
                     static const char typeinfoNameFor[] = " `RTTI Type Descriptor'";
                     if (strstr(demangledTIName, typeinfoNameFor)) {
                        std::string demangledName = demangledTIName;
                        demangledName.erase(demangledName.end() - strlen(typeinfoNameFor), demangledName.end());
                        if (demangledName.compare(0, 6, "class ") == 0)
                           demangledName.erase(0, 6);
                        else if (demangledName.compare(0, 7, "struct ") == 0)
                           demangledName.erase(0, 7);
#else
                  char* demangledTIName = TClassEdit::DemangleName(mangledName.c_str(), errDemangle);
                  if (!errDemangle && demangledTIName) {
                     static const char typeinfoNameFor[] = "typeinfo for ";
                     if (!strncmp(demangledTIName, typeinfoNameFor, strlen(typeinfoNameFor))) {
                        std::string demangledName = demangledTIName + strlen(typeinfoNameFor);
#endif
                        // See the operations in TCling::AutoLoad(type_info)
                        TClassEdit::TSplitType splitname( demangledName.c_str() );
                        splitname.ShortType(demangledName, TClassEdit::kDropStlDefault);

                        if (demangledName != normalizedName && (!reqName || demangledName != reqName)) {
                           if (demangledName.find("Long64_t") == std::string::npos)
                              classesListForRootmap.push_back(demangledName);
                        } // if demangledName != other name
                     } else {
#ifdef WIN32
                        ::CppyyLegacy::TMetaUtils::Error("ExtractClassesListAndDeclLines",
                                                "Demangled typeinfo name '%s' does not contain `RTTI Type Descriptor'\n",
                                                demangledTIName);
#else
                        ::CppyyLegacy::TMetaUtils::Error("ExtractClassesListAndDeclLines",
                                                "Demangled typeinfo name '%s' does not start with 'typeinfo for'\n",
                                                demangledTIName);
#endif
                     } // if demangled type_info starts with "typeinfo for "
                  } // if demangling worked
                  free(demangledTIName);
               } // if mangling worked
            } // if no pseudo-strong typedef involved
         }
      }
   }
   classesListForRootmap.sort();

   // Disable for the moment
   // fwdDeclarationsList = CollapseIdenticalNamespaces(fwdDeclarationsList);

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Loop on selected classes and put them in a list

void ExtractSelectedNamespaces(RScanner &scan, std::list<std::string> &nsList)
{
   for (RScanner::NamespaceColl_t::const_iterator selNsIter = scan.fSelectedNamespaces.begin();
         selNsIter != scan.fSelectedNamespaces.end(); ++selNsIter) {
      nsList.push_back(TMetaUtils::GetQualifiedName(* selNsIter->GetNamespaceDecl()));
   }
}

////////////////////////////////////////////////////////////////////////////////
/// We need annotations even in the PCH: // !, // || etc.

void AnnotateAllDeclsForPCH(cling::Interpreter &interp,
                            RScanner &scan)
{
   auto const & declSelRulesMap = scan.GetDeclsSelRulesMap();
   for (auto const & selClass : scan.fSelectedClasses) {
      // Very important: here we decide if we want to attach attributes to the decl.
      if (clang::CXXRecordDecl *CXXRD =
               llvm::dyn_cast<clang::CXXRecordDecl>(const_cast<clang::RecordDecl *>(selClass.GetRecordDecl()))) {
         AnnotateDecl(*CXXRD, declSelRulesMap, interp, false);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////

int CheckClassesForInterpreterOnlyDicts(cling::Interpreter &interp,
                                        RScanner &scan)
{
   for (auto const & selClass : scan.fSelectedClasses) {
      if (!selClass.GetRecordDecl()->isCompleteDefinition() || selClass.RequestOnlyTClass()) {
         continue;
      }
      const clang::CXXRecordDecl *cxxdecl = llvm::dyn_cast<clang::CXXRecordDecl>(selClass.GetRecordDecl());
      if (cxxdecl && TMetaUtils::ClassInfo__HasMethod(selClass, "Class_Name", interp)) {
         ::CppyyLegacy::TMetaUtils::Error("CheckClassesForInterpreterOnlyDicts",
                                 "Interactivity only dictionaries are not supported for classes with ClassDef\n");
         return 1;
      }
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Make up for skipping RegisterModule, now that dictionary parsing
/// is done and these headers cannot be selected anymore.

int FinalizeStreamerInfoWriting(cling::Interpreter &interp, bool writeEmptyRootPCM=false)
{
   if (!gDriverConfig->fCloseStreamerInfoROOTFile)
      return 0;

   if (interp.parseForModule("#include \"TStreamerInfo.h\"\n"
                             "#include \"TFile.h\"\n"
                             "#include \"TObjArray.h\"\n"
                             "#include \"TVirtualArray.h\"\n"
                             "#include \"TStreamerElement.h\"\n"
                             "#include \"TProtoClass.h\"\n"
                             "#include \"TBaseClass.h\"\n"
                             "#include \"TListOfDataMembers.h\"\n"
                             "#include \"TListOfEnums.h\"\n"
                             "#include \"TListOfEnumsWithLock.h\"\n"
                             "#include \"TDataMember.h\"\n"
                             "#include \"TEnum.h\"\n"
                             "#include \"TEnumConstant.h\"\n"
                             "#include \"TDictAttributeMap.h\"\n"
                             "#include \"TArray.h\"\n"
                             "#include \"root_std_complex.h\"\n")
       != cling::Interpreter::kSuccess)
       return 1;
   if (!gDriverConfig->fCloseStreamerInfoROOTFile(writeEmptyRootPCM)) {
      return 1;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////

int GenerateFullDict(std::ostream &dictStream,
                     cling::Interpreter &interp,
                     RScanner &scan,
                     const TMetaUtils::RConstructorTypes &ctorTypes,
                     bool isSplit,
                     bool isGenreflex,
                     bool writeEmptyRootPCM)
{
   TMetaUtils::TNormalizedCtxt normCtxt(interp.getLookupHelper());

   bool needsCollectionProxy = false;

   //
   // We will loop over all the classes several times.
   // In order we will call
   //
   //     WriteClassInit (code to create the TGenericClassInfo)
   //     check for constructor and operator input
   //     WriteClassFunctions (declared in ClassDef)
   //     WriteClassCode (Streamer,ShowMembers,Auxiliary functions)
   //


   //
   // Loop over all classes and create Streamer() & Showmembers() methods
   //

   // SELECTION LOOP
   for (auto const & ns : scan.fSelectedNamespaces) {
      WriteNamespaceInit(ns, interp, dictStream);
      auto nsName = ns.GetNamespaceDecl()->getQualifiedNameAsString();
      if (nsName.find("(anonymous)") == std::string::npos)
         EmitStreamerInfo(nsName.c_str());
   }

   RScanner::ClassColl_t rejects;
   for (auto const & selClass : scan.fSelectedClasses) {
      if (!selClass.GetRecordDecl()->isCompleteDefinition()) {
         ::CppyyLegacy::TMetaUtils::Error(0, "A dictionary has been requested for %s but there is no declaration!\n", TMetaUtils::GetQualifiedName(selClass).c_str());
         continue;
      }
      if (selClass.RequestOnlyTClass()) {
         // fprintf(stderr,"rootcling: Skipping class %s\n",R__GetQualifiedName(* selClass.GetRecordDecl()).c_str());
         // For now delay those for later.
         continue;
      }

      // Very important: here we decide if we want to attach attributes to the decl.

      if (clang::CXXRecordDecl *CXXRD =
               llvm::dyn_cast<clang::CXXRecordDecl>(const_cast<clang::RecordDecl *>(selClass.GetRecordDecl()))) {
         AnnotateDecl(*CXXRD, scan.GetDeclsSelRulesMap() , interp, isGenreflex);
      }

      const clang::CXXRecordDecl *CRD = llvm::dyn_cast<clang::CXXRecordDecl>(selClass.GetRecordDecl());

      if (CRD) {
         // Workaround: we check for incomplete bases here and reject those class
         // definitions if need be. Otherwise when this is detected down stream, some
         // dictionary code will already have been written
         bool basesOK = true;
         for (auto iter = CRD->bases_begin(), end = CRD->bases_end(); iter != end; ++iter) {
            if (!iter->getType()->getAsCXXRecordDecl()) {
               basesOK = false;
               break;
            }
         }
         if (!basesOK) {
             rejects.push_back(selClass);
             continue;
         }

         ::CppyyLegacy::TMetaUtils::Info(0, "Generating code for class %s\n", selClass.GetNormalizedName());
         if (TMetaUtils::IsStdClass(*CRD) && 0 != TClassEdit::STLKind(CRD->getName().str() /* unqualified name without template argument */)) {
            // Register the collections
            // coverity[fun_call_w_exception] - that's just fine.
            Internal::RStl::Instance().GenerateTClassFor(selClass.GetNormalizedName(), CRD, interp, normCtxt);
         } else {
            TMetaUtils::WriteClassInit(dictStream, selClass, CRD, interp, normCtxt, ctorTypes, needsCollectionProxy);
            EmitStreamerInfo(selClass.GetNormalizedName());
         }
      }
   }

   // Now (temporarily; see end of this function) remove the rejects
   for (auto& rej : rejects) {
       auto pos = std::find(scan.fSelectedClasses.begin(), scan.fSelectedClasses.end(), rej);
       scan.fSelectedClasses.erase(pos);
   }

   //
   // Write all TBuffer &operator>>(...), Class_Name(), Dictionary(), etc.
   // first to allow template specialisation to occur before template
   // instantiation (STK)
   //
   // SELECTION LOOP
   for (auto const & selClass : scan.fSelectedClasses) {

      if (!selClass.GetRecordDecl()->isCompleteDefinition() || selClass.RequestOnlyTClass()) {
         // For now delay those for later.
         continue;
      }
      const clang::CXXRecordDecl *cxxdecl = llvm::dyn_cast<clang::CXXRecordDecl>(selClass.GetRecordDecl());
      if (cxxdecl && TMetaUtils::ClassInfo__HasMethod(selClass, "Class_Name", interp)) {
         WriteClassFunctions(cxxdecl, dictStream, isSplit);
      }
   }

   // LINKDEF SELECTION LOOP
   // Loop to get the shadow class for the class marked 'RequestOnlyTClass' (but not the
   // STL class which is done via Internal::RStl::Instance().WriteClassInit(0);
   // and the ClassInit

   for (auto const & selClass : scan.fSelectedClasses) {
      if (!selClass.GetRecordDecl()->isCompleteDefinition() || !selClass.RequestOnlyTClass()) {
         continue;
      }

      const clang::CXXRecordDecl *CRD = llvm::dyn_cast<clang::CXXRecordDecl>(selClass.GetRecordDecl());

      if (!TMetaUtils::IsSTLContainer(selClass)) {
         TMetaUtils::WriteClassInit(dictStream, selClass, CRD, interp, normCtxt, ctorTypes, needsCollectionProxy);
         EmitStreamerInfo(selClass.GetNormalizedName());
      }
   }
   // Loop to write all the ClassCode
   for (auto const & selClass : scan.fSelectedClasses) {
      TMetaUtils::WriteClassCode(&CallWriteStreamer,
                                       selClass,
                                       interp,
                                       normCtxt,
                                       dictStream,
                                       ctorTypes,
                                       isGenreflex);
   }

   // Loop on the registered collections internally
   // coverity[fun_call_w_exception] - that's just fine.
   Internal::RStl::Instance().WriteClassInit(dictStream, interp, normCtxt, ctorTypes, needsCollectionProxy, EmitStreamerInfo);

   if (!gDriverConfig->fBuildingROOTStage1) {
      EmitTypedefs(scan.fSelectedTypedefs);
      EmitEnums(scan.fSelectedEnums);
      // Make up for skipping RegisterModule, now that dictionary parsing
      // is done and these headers cannot be selected anymore.
      int finRetCode = FinalizeStreamerInfoWriting(interp, writeEmptyRootPCM);
      if (finRetCode != 0) return finRetCode;
   }

   // Re-insert rejects, so that headers etc. are still added
   for (auto& rej : rejects) {
       scan.fSelectedClasses.push_back(rej);
   }

   return 0;
}

////////////////////////////////////////////////////////////////////////////////

void CreateDictHeader(std::ostream &dictStream, const std::string &main_dictname)
{
   dictStream  << "// Do NOT change. Changes will be lost next time file is generated\n\n"
               << "#define R__DICTIONARY_FILENAME " << main_dictname << std::endl

               // We do not want deprecation warnings to fire in dictionaries
               << "#define R__NO_DEPRECATION" << std::endl

               // Now that CINT is not longer there to write the header file,
               // write one and include in there a few things for backward
               // compatibility.
               << "\n/*******************************************************************/\n"
               << "#include <stddef.h>\n"
               << "#include <stdio.h>\n"
               << "#include <stdlib.h>\n"
               << "#include <string.h>\n"
               << "#include <assert.h>\n"
               << "#define G__DICTIONARY\n"
               << "#include \"RConfig.h\"\n"
               << "#include \"TClass.h\"\n"
               << "#include \"TDictAttributeMap.h\"\n"
               << "#include \"TInterpreter.h\"\n"
               << "#include \"TROOT.h\"\n"
               << "#include \"TBuffer.h\"\n"
               << "#include \"TMemberInspector.h\"\n"
               << "#include \"TInterpreter.h\"\n"
               << "#include \"TVirtualMutex.h\"\n"
               << "#include \"TError.h\"\n\n"
               << "#ifndef G__ROOT\n"
               << "#define G__ROOT\n"
               << "#endif\n\n"
               << "#include \"RtypesImp.h\"\n"
               << "#include \"TIsAProxy.h\"\n"
               << "#include <algorithm>\n"
               << "#include \"TCollectionProxyInfo.h\"\n"
               << "/*******************************************************************/\n\n"
               << "#include \"TDataMember.h\"\n\n"; // To set their transiency
}

////////////////////////////////////////////////////////////////////////////////

void GenerateNecessaryIncludes(std::ostream &dictStream,
                               const std::string &includeForSource,
                               const std::string &extraIncludes)
{
   dictStream << "// Header files passed as explicit arguments\n"
              << includeForSource << std::endl
              << "// Header files passed via #pragma extra_include\n"
              << extraIncludes << std::endl;
}

//______________________________________________________________________________

// cross-compiling for iOS and iOS simulator (assumes host is Intel Mac OS X)
#if defined(R__IOSSIM) || defined(R__IOS)
#ifdef __x86_64__
#undef __x86_64__
#endif
#ifdef __i386__
#undef __i386__
#endif
#ifdef R__IOSSIM
#define __i386__ 1
#endif
#ifdef R__IOS
#define __arm__ 1
#endif
#endif

////////////////////////////////////////////////////////////////////////////////
/// Little helper class to bookkeep the files names which we want to make
/// temporary.

class tempFileNamesCatalog {
public:
   //______________________________________________
   tempFileNamesCatalog(): m_size(0), m_emptyString("") {};

   std::string getTmpFileName(const std::string &filename) {
      return filename + "_tmp_" + std::to_string(getpid());
   }
   /////////////////////////////////////////////////////////////////////////////
   /// Adds the name and the associated temp name to the catalog.
   /// Changes the name into the temp name

   void addFileName(std::string &nameStr) {
      if (nameStr.empty()) return;

      std::string tmpNameStr(getTmpFileName(nameStr));

      // For brevity
      const char *name(nameStr.c_str());
      const char *tmpName(tmpNameStr.c_str());

      m_names.push_back(nameStr);
      m_tempNames.push_back(tmpNameStr);
      ::CppyyLegacy::TMetaUtils::Info(0, "File %s added to the tmp catalog.\n", name);

      // This is to allow update of existing files
      if (0 == std::rename(name , tmpName)) {
         ::CppyyLegacy::TMetaUtils::Info(0, "File %s existing. Preserved as %s.\n", name, tmpName);
      }

      // To change the name to its tmp version
      nameStr = tmpNameStr;

      m_size++;

   }

   /////////////////////////////////////////////////////////////////////////////

   int clean() {
      int retval = 0;
      // rename the temp files into the normal ones
      for (unsigned int i = 0; i < m_size; ++i) {
         const char *tmpName = m_tempNames[i].c_str();
         // Check if the file exists
         std::ifstream ifile(tmpName);
         if (!ifile)
            ::CppyyLegacy::TMetaUtils::Error(0, "Cannot find %s!\n", tmpName);

         if (0 != std::remove(tmpName)) {
            ::CppyyLegacy::TMetaUtils::Error(0, "Removing %s!\n", tmpName);
            retval++;
         }
      }
      return retval;
   }

   /////////////////////////////////////////////////////////////////////////////

   int commit() {
      int retval = 0;
      // rename the temp files into the normal ones
      for (unsigned int i = 0; i < m_size; ++i) {
         const char *tmpName = m_tempNames[i].c_str();
         const char *name = m_names[i].c_str();
         // Check if the file exists
         std::ifstream ifile(tmpName);
         if (!ifile)
            ::CppyyLegacy::TMetaUtils::Error(0, "Cannot find %s!\n", tmpName);
#ifdef WIN32
         // Sometimes files cannot be renamed on Windows if they don't have
         // been released by the system. So just copy them and try to delete
         // the old one afterwards.
         if (ifile.is_open())
            ifile.close();
         if (0 != std::rename(tmpName , name)) {
            if (llvm::sys::fs::copy_file(tmpName , name)) {
               llvm::sys::fs::remove(tmpName);
            }
         }
#else
         if (0 != std::rename(tmpName , name)) {
            ::CppyyLegacy::TMetaUtils::Error(0, "Renaming %s into %s!\n", tmpName, name);
            retval++;
         }
#endif
      }
      return retval;
   }

   /////////////////////////////////////////////////////////////////////////////

   const std::string &getFileName(const std::string &tmpFileName) {
      size_t i = std::distance(m_tempNames.begin(),
                               find(m_tempNames.begin(), m_tempNames.end(), tmpFileName));
      if (i == m_tempNames.size()) return m_emptyString;
      return m_names[i];
   }

   /////////////////////////////////////////////////////////////////////////////

   void dump() {
      std::cout << "Restoring files in temporary file catalog:\n";
      for (unsigned int i = 0; i < m_size; ++i) {
         std::cout <<  m_tempNames[i] << " --> " << m_names[i] << std::endl;
      }
   }

private:
   unsigned int m_size;
   const std::string m_emptyString;
   std::vector<std::string> m_names;
   std::vector<std::string> m_tempNames;
};

////////////////////////////////////////////////////////////////////////////////
/// Transform name of dictionary

std::ostream *CreateStreamPtrForSplitDict(const std::string &dictpathname,
      tempFileNamesCatalog &tmpCatalog)
{
   std::string splitDictName(tmpCatalog.getFileName(dictpathname));
   const size_t dotPos = splitDictName.find_last_of(".");
   splitDictName.insert(dotPos, "_classdef");
   tmpCatalog.addFileName(splitDictName);
   return new std::ofstream(splitDictName.c_str());
}

////////////////////////////////////////////////////////////////////////////////
/// Transform -W statements in diagnostic pragmas for cling reacting on "-Wno-"
/// For example
/// -Wno-deprecated-declarations --> #pragma clang diagnostic ignored "-Wdeprecated-declarations"

static void CheckForMinusW(std::string arg,
                           std::list<std::string> &diagnosticPragmas)
{
   static const std::string pattern("-Wno-");

   if (arg.find(pattern) != 0)
      return;
   if (arg == "-Wno-noexcept-type") {
      // GCC7 warning not supported by clang 3.9
      return;
   }

   TMetaUtils::ReplaceAll(arg, pattern, "#pragma clang diagnostic ignored \"-W");
   arg += "\"";
   diagnosticPragmas.push_back(arg);
}

////////////////////////////////////////////////////////////////////////////////

std::string GetFwdDeclnArgsToKeepString(const TMetaUtils::TNormalizedCtxt &normCtxt,
                                        cling::Interpreter &interp)
{
   using namespace CppyyLegacy::TMetaUtils::AST2SourceTools;
   std::string fwdDecl;
   std::string initStr("{");
   auto &fwdDeclnArgsToSkipColl = normCtxt.GetTemplNargsToKeepMap();
   for (auto & strigNargsToKeepPair : fwdDeclnArgsToSkipColl) {
      auto &clTemplDecl = *strigNargsToKeepPair.first;
      FwdDeclFromTmplDecl(clTemplDecl , interp, fwdDecl);
      initStr += "{\"" +
                 fwdDecl + "\", "
                 + std::to_string(strigNargsToKeepPair.second)
                 + "},";
   }
   if (!fwdDeclnArgsToSkipColl.empty())
      initStr.pop_back();
   initStr += "}";
   return initStr;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the pointee type if possible

clang::QualType GetPointeeTypeIfPossible(const clang::QualType &qt)
{
   if (qt.isNull()) return qt;
   clang::QualType thisQt(qt);
   while (thisQt->isPointerType() ||
          thisQt->isReferenceType()) {
      thisQt = thisQt->getPointeeType();
   }
   return thisQt;

}

////////////////////////////////////////////////////////////////////////////////
/// Extract the list of headers necessary for the Decl

std::list<std::string> RecordDecl2Headers(const clang::CXXRecordDecl &rcd,
      const cling::Interpreter &interp,
      std::set<const clang::CXXRecordDecl *> &visitedDecls)
{
   std::list<std::string> headers;

   // We push a new transaction because we could deserialize decls here
   cling::Interpreter::PushTransactionRAII RAII(&interp);

   // Avoid infinite recursion
   if (!visitedDecls.insert(rcd.getCanonicalDecl()).second)
      return headers;

   // If this is a template
   if (const clang::ClassTemplateSpecializationDecl *tsd = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&rcd)) {

      // Loop on the template args
      for (auto & tArg : tsd->getTemplateArgs().asArray()) {
         if (clang::TemplateArgument::ArgKind::Type != tArg.getKind()) continue;
         auto tArgQualType = GetPointeeTypeIfPossible(tArg.getAsType());
         if (tArgQualType.isNull()) continue;
         if (const clang::CXXRecordDecl *tArgCxxRcd = tArgQualType->getAsCXXRecordDecl()) {
            headers.splice(headers.end(), RecordDecl2Headers(*tArgCxxRcd, interp, visitedDecls));
         }
      }

      if (!TMetaUtils::IsStdClass(rcd) && rcd.hasDefinition()) {

         // Loop on base classes - with a newer llvm, range based possible
         for (auto baseIt = tsd->bases_begin(); baseIt != tsd->bases_end(); baseIt++) {
            auto baseQualType = GetPointeeTypeIfPossible(baseIt->getType());
            if (baseQualType.isNull()) continue;
            if (const clang::CXXRecordDecl *baseRcdPtr = baseQualType->getAsCXXRecordDecl()) {
               headers.splice(headers.end(), RecordDecl2Headers(*baseRcdPtr, interp, visitedDecls));
            }
         }

         // Loop on the data members - with a newer llvm, range based possible
         for (auto declIt = tsd->decls_begin(); declIt != tsd->decls_end(); ++declIt) {
            if (const clang::FieldDecl *fieldDecl = llvm::dyn_cast<clang::FieldDecl>(*declIt)) {
               auto fieldQualType = GetPointeeTypeIfPossible(fieldDecl->getType());
               if (fieldQualType.isNull()) continue ;
               if (const clang::CXXRecordDecl *fieldCxxRcd = fieldQualType->getAsCXXRecordDecl()) {
                  if (fieldCxxRcd->hasDefinition())
                     headers.splice(headers.end(), RecordDecl2Headers(*fieldCxxRcd, interp, visitedDecls));
               }
            }
         }

         // Loop on methods
         for (auto methodIt = tsd->method_begin(); methodIt != tsd->method_end(); ++methodIt) {
            // Check arguments
            for (auto & fPar : methodIt->parameters()) {
               auto fParQualType = GetPointeeTypeIfPossible(fPar->getOriginalType());
               if (fParQualType.isNull()) continue;
               if (const clang::CXXRecordDecl *fParCxxRcd = fParQualType->getAsCXXRecordDecl()) {
                  if (fParCxxRcd->hasDefinition())
                     headers.splice(headers.end(), RecordDecl2Headers(*fParCxxRcd, interp, visitedDecls));
               }
            }
            // Check return value
            auto retQualType = GetPointeeTypeIfPossible(methodIt->getReturnType());
            if (retQualType.isNull()) continue;
            if (const clang::CXXRecordDecl *retCxxRcd = retQualType->getAsCXXRecordDecl()) {
               if (retCxxRcd->hasDefinition())
                  headers.splice(headers.end(), RecordDecl2Headers(*retCxxRcd, interp, visitedDecls));
            }
         }
      }

   } // End template instance

   std::string header = TMetaUtils::GetFileName(rcd, interp);
   headers.emplace_back(header);
   headers.reverse();
   return headers;

}

////////////////////////////////////////////////////////////////////////////////
/// Check if the class good for being an autoparse key.
/// We exclude from this set stl containers of pods/strings
/// TODO: we may use also __gnu_cxx::
bool IsGoodForAutoParseMap(const clang::RecordDecl& rcd){

   // If it's not an std class, we just pick it up.
   if (auto dclCtxt= rcd.getDeclContext()){
      if (! dclCtxt->isStdNamespace()){
         return true;
      }
   } else {
      return true;
   }

   // Now, we have a stl class. We now check if it's a template. If not, we
   // do not take it: bitset, string and so on.
   auto clAsTmplSpecDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&rcd);
   if (!clAsTmplSpecDecl) return false;

   // Now we have a template in the stl. Let's see what the arguments are.
   // If they are not a POD or something which is good for autoparsing, we keep
   // them.
   auto& astCtxt = rcd.getASTContext();
   auto& templInstArgs = clAsTmplSpecDecl->getTemplateInstantiationArgs();
   for (auto&& arg : templInstArgs.asArray()){

      auto argKind = arg.getKind();
      if (argKind != clang::TemplateArgument::Type){
         if (argKind == clang::TemplateArgument::Integral) continue;
         else return true;
      }

      auto argQualType = arg.getAsType();
      auto isPOD = argQualType.isPODType(astCtxt);
      // This is a POD, we can inspect the next arg
      if (isPOD) continue;

      auto argType = argQualType.getTypePtr();
      if (auto recType = llvm::dyn_cast<clang::RecordType>(argType)){
         auto isArgGoodForAutoParseMap = IsGoodForAutoParseMap(*recType->getDecl());
         // The arg is a class but good for the map
         if (isArgGoodForAutoParseMap) continue;
      } else {
         // The class is not a POD nor a class we can skip
         return true;
      }
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////

void ExtractHeadersForDecls(const RScanner::ClassColl_t &annotatedRcds,
                            const RScanner::TypedefColl_t tDefDecls,
                            const RScanner::FunctionColl_t funcDecls,
                            const RScanner::VariableColl_t varDecls,
                            const RScanner::EnumColl_t enumDecls,
                            HeadersDeclsMap_t &headersClassesMap,
                            HeadersDeclsMap_t &headersDeclsMap,
                            const cling::Interpreter &interp)
{
   std::set<const clang::CXXRecordDecl *> visitedDecls;
   std::unordered_set<std::string> buffer;
   std::string autoParseKey;

   // Add some manip of headers
   for (auto & annotatedRcd : annotatedRcds) {
      if (const clang::CXXRecordDecl *cxxRcd =
               llvm::dyn_cast_or_null<clang::CXXRecordDecl>(annotatedRcd.GetRecordDecl())) {
         autoParseKey = "";
         visitedDecls.clear();
         std::list<std::string> headers(RecordDecl2Headers(*cxxRcd, interp, visitedDecls));
         // remove duplicates, also if not subsequent
         buffer.clear();
         headers.remove_if([&buffer](const std::string & s) {
            return !buffer.insert(s).second;
         });
         GetMostExternalEnclosingClassName(*cxxRcd, autoParseKey, interp);
         if (autoParseKey.empty()) autoParseKey = annotatedRcd.GetNormalizedName();
         if (IsGoodForAutoParseMap(*cxxRcd)){
            headersDeclsMap[autoParseKey] = headers;
            headersDeclsMap[annotatedRcd.GetRequestedName()] = headers;
         } else {
            ::CppyyLegacy::TMetaUtils::Info(0, "Class %s is not included in the set of autoparse keys.\n", autoParseKey.c_str());
         }

         // Propagate to the classes map only if this is not a template.
         // The header is then used as autoload key and we want to avoid duplicates.
         if (!llvm::isa<clang::ClassTemplateSpecializationDecl>(cxxRcd)){
            headersClassesMap[autoParseKey] = headersDeclsMap[autoParseKey];
            headersClassesMap[annotatedRcd.GetRequestedName()] = headersDeclsMap[annotatedRcd.GetRequestedName()];
         }
      }
   }

   // The same for the typedefs:
   for (auto & tDef : tDefDecls) {
      if (clang::CXXRecordDecl *cxxRcd = tDef->getUnderlyingType()->getAsCXXRecordDecl()) {
         autoParseKey = "";
         visitedDecls.clear();
         std::list<std::string> headers(RecordDecl2Headers(*cxxRcd, interp, visitedDecls));
         headers.push_back(TMetaUtils::GetFileName(*tDef, interp));
         // remove duplicates, also if not subsequent
         buffer.clear();
         headers.remove_if([&buffer](const std::string & s) {
            return !buffer.insert(s).second;
         });
         GetMostExternalEnclosingClassNameFromDecl(*tDef, autoParseKey, interp);
         if (autoParseKey.empty()) autoParseKey = tDef->getQualifiedNameAsString();
         headersDeclsMap[autoParseKey] = headers;
      }
   }

   // The same for the functions:
   for (auto & func : funcDecls) {
      std::list<std::string> headers = {TMetaUtils::GetFileName(*func, interp)};
      headersDeclsMap[TMetaUtils::GetQualifiedName(*func)] = headers;
   }

   // The same for the variables:
   for (auto & var : varDecls) {
      std::list<std::string> headers = {TMetaUtils::GetFileName(*var, interp)};
      headersDeclsMap[TMetaUtils::GetQualifiedName(*var)] = headers;
   }

   // The same for the enums:
   for (auto & en : enumDecls) {
      std::list<std::string> headers = {TMetaUtils::GetFileName(*en, interp)};
      headersDeclsMap[TMetaUtils::GetQualifiedName(*en)] = headers;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Generate the fwd declarations of the selected entities

static std::string GenerateFwdDeclString(const RScanner &scan,
                                         const cling::Interpreter &interp)
{
   std::string newFwdDeclString;

   using namespace CppyyLegacy::TMetaUtils::AST2SourceTools;

   std::string fwdDeclString;
   std::string buffer;
   std::unordered_set<std::string> fwdDecls;

   // Classes

   // Build the input for a transaction containing all of the selected declarations
   // Cling will produce the fwd declaration payload.

   std::vector<const clang::Decl *> selectedDecls(scan.fSelectedClasses.size());

   // Pick only RecordDecls
   std::transform (scan.fSelectedClasses.begin(),
                   scan.fSelectedClasses.end(),
                   selectedDecls.begin(),
                   [](const TMetaUtils::AnnotatedRecordDecl& rcd){return rcd.GetRecordDecl();});

   for (auto* TD: scan.fSelectedTypedefs) {
   // for typedefs of template instantiations, forward declarations are not
   // good enough: add full headers instead.
       clang::QualType t = TD->getUnderlyingType();
       clang::CXXRecordDecl* decl = t->getAsCXXRecordDecl();
       if (!decl || decl->isInStdNamespace() /* standard headers are precompiled */ || \
               (decl->getKind() != clang::Decl::ClassTemplatePartialSpecialization && \
                decl->getKind() != clang::Decl::ClassTemplateSpecialization)) {
           selectedDecls.push_back(TD);
           continue;
       }

       clang::SourceManager& s = decl->getASTContext().getSourceManager();
       if (!s.isInSystemHeader(decl->getLocation())) { // system hdrs are precompiled
           selectedDecls.push_back(TD);
           continue;
       }

       clang::TypeSourceInfo* tsi = TD->getTypeSourceInfo();
       if (tsi) {
           clang::FileID fid = s.getFileID(tsi->getTypeLoc().getBeginLoc());
           clang::SourceLocation incloc = s.getIncludeLoc(fid);
           if (fid.isValid() && incloc.isValid()) {
               bool Invalid = false;
               const char* p = s.getCharacterData(incloc, &Invalid);
               if (!Invalid) {
                   const char* q = p;
                   while (*q != '\n' && *q != '\0') ++q;
                   if (q-p) {
                       fwdDeclString += "#include ";
                       fwdDeclString += std::string(p, q-p+1);
                       continue;
                   }
               }
           }
       }

       selectedDecls.push_back(TD);
   }

   // The "R\"DICTFWDDCLS(\n" ")DICTFWDDCLS\"" pieces have been moved to
   // TModuleGenerator to be able to make the diagnostics more telling in presence
   // of an issue ROOT-6752.
   fwdDeclString += Decls2FwdDecls(selectedDecls,IsLinkdefFile,interp);

   if (fwdDeclString.empty()) fwdDeclString = "";
   return fwdDeclString;
}

////////////////////////////////////////////////////////////////////////////////
/// Generate a string for the dictionary from the headers-classes map.

const std::string GenerateStringFromHeadersForClasses(const HeadersDeclsMap_t &headersClassesMap,
      const std::string &detectedUmbrella,
      bool payLoadOnly = false)
{
   std::string headerName;

   if (genreflex::verbose)
      std::cout << "Class-headers Mapping:\n";
   std::string headersClassesMapString = "";
   for (auto const & classHeaders : headersClassesMap) {
      if (genreflex::verbose)
         std::cout << " o " << classHeaders.first << " --> ";
      headersClassesMapString += "\"";
      headersClassesMapString += classHeaders.first + "\"";
      for (auto const & header : classHeaders.second) {
         headerName = (detectedUmbrella == header || payLoadOnly) ? "payloadCode" : "\"" + header + "\"";
         headersClassesMapString += ", " + headerName;
         if (genreflex::verbose)
            std::cout << ", " << headerName;
         if (payLoadOnly)
            break;
      }
      if (genreflex::verbose)
         std::cout << std::endl;
      headersClassesMapString += ", \"@\",\n";
   }
   headersClassesMapString += "nullptr";
   return headersClassesMapString;
}

////////////////////////////////////////////////////////////////////////////////

bool IsImplementationName(const std::string &filename)
{
   return !TMetaUtils::IsHeaderName(filename);
}

////////////////////////////////////////////////////////////////////////////////
/// Check if the argument is a sane cling argument. Performing the following checks:
/// 1) It does not start with "--" and is not the --param option.

bool IsCorrectClingArgument(const std::string& argument)
{
   if (TMetaUtils::BeginsWith(argument,"--") && !TMetaUtils::BeginsWith(argument,"--param")) return false;
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool NeedsSelection(const char* name)
{
   static const std::vector<std::string> namePrfxes {
      "array<",
      "unique_ptr<"};
   auto pos = find_if(namePrfxes.begin(),
                      namePrfxes.end(),
                      [&](const std::string& str){return TMetaUtils::BeginsWith(name,str);});
   return namePrfxes.end() == pos;
}

////////////////////////////////////////////////////////////////////////////////

bool IsSupportedClassName(const char* name)
{
   static const std::vector<std::string> uclNamePrfxes {
      "chrono:",
      "ratio<",
      "shared_ptr<"};
   static const std::set<std::string> unsupportedClassesNormNames{
      "regex",
      "thread"};
   if ( unsupportedClassesNormNames.count(name) == 1) return false;
   auto pos = find_if(uclNamePrfxes.begin(),
                      uclNamePrfxes.end(),
                      [&](const std::string& str){return TMetaUtils::BeginsWith(name,str);});
   return uclNamePrfxes.end() == pos;
}

////////////////////////////////////////////////////////////////////////////////
/// Check if the list of selected classes contains any class which is not
/// supported. Return the number of unsupported classes in the selection.

int CheckForUnsupportedClasses(const RScanner::ClassColl_t &annotatedRcds)
{
   int nerrors = 0;
   for (auto&& aRcd : annotatedRcds){
      auto clName = aRcd.GetNormalizedName();
      if (!IsSupportedClassName(clName)){
         std::cerr << "Error: Class " << clName << " has been selected but "
               << "currently the support for its I/O is not yet available. Note that "
               << clName << ", even if not selected, will be available for "
               << "interpreted code.\n";
         nerrors++;
      }
      if (!NeedsSelection(clName)){
         std::cerr << "Error: It is not necessary to explicitly select class "
                   << clName << ". I/O is supported for it transparently.\n";
         nerrors++;
      }
   }
   return nerrors;
}

////////////////////////////////////////////////////////////////////////////////

class TRootClingCallbacks : public cling::InterpreterCallbacks {
private:
    std::list<std::string>& fFilesIncludedByLinkdef;
    bool isLocked = false;
public:
    TRootClingCallbacks(cling::Interpreter* interp, std::list<std::string>& filesIncludedByLinkdef):
      InterpreterCallbacks(interp),
      fFilesIncludedByLinkdef(filesIncludedByLinkdef){};

   ~TRootClingCallbacks(){};

   virtual void InclusionDirective(clang::SourceLocation /*HashLoc*/, const clang::Token & /*IncludeTok*/,
                                   llvm::StringRef FileName, bool IsAngled, clang::CharSourceRange /*FilenameRange*/,
                                   clang::OptionalFileEntryRef /*File*/, llvm::StringRef /*SearchPath*/,
                                   llvm::StringRef /*RelativePath*/, const clang::Module * /*Imported*/,
                                   clang::SrcMgr::CharacteristicKind /*FileType*/) override
   {
      if (isLocked) return;
      if (IsAngled) return;
      auto& PP = m_Interpreter->getCI()->getPreprocessor();
      auto curLexer = PP.getCurrentFileLexer();
      if (!curLexer) return;
      auto fileEntry = curLexer->getFileEntry();
      if (!fileEntry) return;
      auto thisFileName = fileEntry->getName();
      auto fileNameAsString = FileName.str();
      auto isThisLinkdef = TMetaUtils::IsLinkdefFile(thisFileName.data());
      if (isThisLinkdef) {
         auto isTheIncludedLinkdef = TMetaUtils::IsLinkdefFile(fileNameAsString.c_str());
         if (isTheIncludedLinkdef) {
            fFilesIncludedByLinkdef.clear();
            isLocked = true;
         } else {
            fFilesIncludedByLinkdef.emplace_back(fileNameAsString.c_str());
         }
      }
   }

   // rootcling pre-includes things such as Rtypes.h. This means that ACLiC can
   // call rootcling asking it to create a module for a file with no #includes
   // but relying on things from Rtypes.h such as the ClassDef macro.
   //
   // When rootcling starts building a module, it becomes resilient to the
   // outside environment and pre-included files have no effect. This hook
   // informs rootcling when a new submodule is being built so that it can
   // make CoreLegacy.Rtypes.h visible.
   void EnteredSubmodule(clang::Module* M,
                         clang::SourceLocation ImportLoc,
                         bool ForPragma) override {
      assert(M);
      using namespace clang;
      if (llvm::StringRef(M->Name).endswith("ACLiC_dict")) {
         Preprocessor& PP = m_Interpreter->getCI()->getPreprocessor();
         HeaderSearch& HS = PP.getHeaderSearchInfo();
         // FIXME: Reduce to CoreLegacy.Rtypes.h.
         Module* CoreModule = HS.lookupModule("CoreLegacy", SourceLocation(),
                                              /*AllowSearch*/false);
         assert(M && "Must have module CoreLegacy");
         PP.makeModuleVisible(CoreModule, ImportLoc);
      }
   }
};

////////////////////////////////////////////////////////////////////////////////
/// Custom diag client for clang that verifies that each implicitly build module
/// is a system module. If not, it will let the current rootcling invocation
/// fail with an error. All other diags beside module build remarks will be
/// forwarded to the passed child diag client.
///
/// The reason why we need this is that if we built implicitly a C++ module
/// that belongs to a ROOT dictionary, then we will miss information generated
/// by rootcling in this file (e.g. the source code comments to annotation
/// attributes transformation will be missing in the module file).
class CheckModuleBuildClient : public clang::DiagnosticConsumer {
   clang::DiagnosticConsumer *fChild;
   bool fOwnsChild;
   clang::ModuleMap &fMap;

public:
   CheckModuleBuildClient(clang::DiagnosticConsumer *Child, bool OwnsChild, clang::ModuleMap &Map)
      : fChild(Child), fOwnsChild(OwnsChild), fMap(Map)
   {
   }

   ~CheckModuleBuildClient()
   {
      if (fOwnsChild)
         delete fChild;
   }

   virtual void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel, const clang::Diagnostic &Info) override
   {
      using namespace clang::diag;

      // This method catches the module_build remark from clang and checks if
      // the implicitly built module is a system module or not. We only support
      // building system modules implicitly.

      std::string moduleName;
      const clang::Module *module = nullptr;

      // Extract the module from the diag argument with index 0.
      const auto &ID = Info.getID();
      if (ID == remark_module_build || ID == remark_module_build_done) {
         moduleName = Info.getArgStdStr(0);
         module = fMap.findModule(moduleName);
         // We should never be able to build a module without having it in the
         // modulemap. Still, let's print a warning that we at least tell the
         // user that this could lead to problems.
         if (!module) {
            ::CppyyLegacy::TMetaUtils::Warning(0,
                                      "Couldn't find module %s in the available modulemaps. This"
                                      "prevents us from correctly diagnosing wrongly built modules.\n",
                                      moduleName.c_str());
         }
      }

      // Skip the diag only if we build a ROOT system module or a system module. We still print the diag
      // when building a non-system module as we will print an error below and the
      // user should see the detailed default clang diagnostic.
      bool isROOTSystemModuleDiag = module && llvm::StringRef(moduleName).startswith("ROOT_");
      bool isSystemModuleDiag = module && module->IsSystem;
      if (!isROOTSystemModuleDiag && !isSystemModuleDiag)
         fChild->HandleDiagnostic(DiagLevel, Info);

      if (ID == remark_module_build && !isROOTSystemModuleDiag && !isSystemModuleDiag) {
         ::CppyyLegacy::TMetaUtils::Error(0,
                                 "Had to build non-system module %s implicitly. You first need to\n"
                                 "generate the dictionary for %s or mark the C++ module as a system\n"
                                 "module if you provided your own system modulemap file:\n"
                                 "%s [system] { ... }\n",
                                 moduleName.c_str(), moduleName.c_str(), moduleName.c_str());
      }
   }

   // All methods below just forward to the child and the default method.
   virtual void clear() override
   {
      fChild->clear();
      DiagnosticConsumer::clear();
   }

   virtual void BeginSourceFile(const clang::LangOptions &LangOpts, const clang::Preprocessor *PP) override
   {
      fChild->BeginSourceFile(LangOpts, PP);
      DiagnosticConsumer::BeginSourceFile(LangOpts, PP);
   }

   virtual void EndSourceFile() override
   {
      fChild->EndSourceFile();
      DiagnosticConsumer::EndSourceFile();
   }

   virtual void finish() override
   {
      fChild->finish();
      DiagnosticConsumer::finish();
   }

   virtual bool IncludeInDiagnosticCounts() const override { return fChild->IncludeInDiagnosticCounts(); }
};

static void MaybeSuppressWin32CrashDialogs() {
#if defined(_WIN32) && defined(_MSC_VER)
   // Suppress error dialogs to avoid hangs on build nodes.
   // One can use an environment variable (Cling_GuiOnAssert) to enable
   // the error dialogs.
   const char *EnablePopups = getenv("Cling_GuiOnAssert");
   if (EnablePopups == nullptr || EnablePopups[0] == '0') {
      ::_set_error_mode(_OUT_TO_STDERR);
      _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
   }
#endif
}

static llvm::cl::OptionCategory gRootclingOptions("rootcling common options");
static llvm::cl::opt<bool> gOptForce("f", llvm::cl::desc("Overwrite <file>s."),
                                    llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool> gOptRootBuild("rootbuild", llvm::cl::desc("If we are building ROOT."),
                                        llvm::cl::Hidden,
                                        llvm::cl::cat(gRootclingOptions));
enum VerboseLevel {
   v = TMetaUtils::kError,
   v0 = TMetaUtils::kFatal,
   v1 = v,
   v2 = TMetaUtils::kWarning,
   v3 = TMetaUtils::kNote,
   v4 = TMetaUtils::kInfo
};
static llvm::cl::opt<VerboseLevel>
gOptVerboseLevel(llvm::cl::desc("Choose verbosity level:"),
                llvm::cl::values(clEnumVal(v, "Show errors."),
                                 clEnumVal(v0, "Show only fatal errors."),
                                 clEnumVal(v1, "Show errors (the same as -v)."),
                                 clEnumVal(v2, "Show warnings (default)."),
                                 clEnumVal(v3, "Show notes."),
                                 clEnumVal(v4, "Show information.")),
                llvm::cl::init(v2),
                llvm::cl::cat(gRootclingOptions));

static llvm::cl::opt<bool>
gOptCint("cint", llvm::cl::desc("Deprecated, legacy flag which is ignored."),
        llvm::cl::Hidden,
        llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptReflex("reflex", llvm::cl::desc("Deprecated, legacy flag which is ignored."),
          llvm::cl::Hidden,
          llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptGccXml("gccxml", llvm::cl::desc("Deprecated, legacy flag which is ignored."),
          llvm::cl::Hidden,
          llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<std::string>
gOptLibListPrefix("lib-list-prefix",
                 llvm::cl::desc("An ACLiC feature which exports the list of dependent libraries."),
                 llvm::cl::Hidden,
                 llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptGeneratePCH("generate-pch",
               llvm::cl::desc("Generates a pch file from a predefined set of headers. See makepch.py."),
               llvm::cl::Hidden,
               llvm::cl::cat(gRootclingOptions));
 // FIXME: We should remove the IgnoreExistingDict option as it is not used.
static llvm::cl::opt<bool>
gOptIgnoreExistingDict("r",
               llvm::cl::desc("Deprecated, legacy flag which is ignored."),
               llvm::cl::Hidden,
               llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<std::string>
gOptDictionaryFileName(llvm::cl::Positional, llvm::cl::Required,
                      llvm::cl::desc("<output dictionary file>"),
                      llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptC("c", llvm::cl::desc("Deprecated, legacy flag which is ignored."),
     llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptRootmapLibNames("rml", llvm::cl::ZeroOrMore,
                   llvm::cl::desc("Generate rootmap file."),
                   llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<std::string>
gOptRootMapFileName("rmf",
                   llvm::cl::desc("Generate a rootmap file with the specified name."),
                   llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptCxxModule("cxxmodule",
             llvm::cl::desc("Generate a C++ module."),
             llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptModuleMapFiles("moduleMapFile",
                   llvm::cl::desc("Specify a C++ modulemap file."),
                   llvm::cl::cat(gRootclingOptions));
// FIXME: Figure out how to combine the code of -umbrellaHeader and inlineInputHeader
static llvm::cl::opt<bool>
gOptUmbrellaInput("umbrellaHeader",
                  llvm::cl::desc("A single header including all headers instead of specifying them on the command line."),
                  llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptMultiDict("multiDict",
             llvm::cl::desc("If this library has multiple separate LinkDef files."),
             llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptInterpreterOnly("interpreteronly",
                   llvm::cl::desc("Generate minimal dictionary for interactivity (without IO information)."),
                   llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptSplit("split",
         llvm::cl::desc("Split the dictionary into two parts: one containing the IO (ClassDef)\
information and another the interactivity support."),
         llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptNoDictSelection("noDictSelection",
                   llvm::cl::Hidden,
                   llvm::cl::desc("Do not run the selection rules. Useful when in -onepcm mode."),
                   llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<std::string>
gOptSharedLibFileName("s",
                     llvm::cl::desc("The path to the library of the built dictionary."),
                     llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptModuleDependencies("m",
                      llvm::cl::desc("The list of dependent modules of the dictionary."),
                      llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptExcludePaths("excludePath", llvm::cl::ZeroOrMore,
                llvm::cl::desc("Do not store the <path> in the dictionary."),
                llvm::cl::cat(gRootclingOptions));
// FIXME: This does not seem to work. We have one use of -inlineInputHeader in
// ROOT and it does not produce the expected result.
static llvm::cl::opt<bool>
gOptInlineInput("inlineInputHeader",
               llvm::cl::desc("Does not generate #include <header> but expands the header content."),
               llvm::cl::cat(gRootclingOptions));
// FIXME: This is totally the wrong concept. We should not expose an interface
// to be able to tell which component is in the pch and which needs extra
// scaffolding for interactive use. Moreover, some of the ROOT components are
// partially in the pch and this option makes it impossible to express that.
// We should be able to get the list of headers in the pch early and scan
// through them.
static llvm::cl::opt<bool>
gOptWriteEmptyRootPCM("writeEmptyRootPCM",
                     llvm::cl::Hidden,
                     llvm::cl::desc("Does not include the header files as it assumes they exist in the pch."),
                     llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptCheckSelectionSyntax("selSyntaxOnly",
                        llvm::cl::desc("Check the selection syntax only."),
                        llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptFailOnWarnings("failOnWarnings",
                  llvm::cl::desc("Fail if there are warnings."),
                  llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<bool>
gOptNoIncludePaths("noIncludePaths",
                  llvm::cl::desc("Do not store include paths but rely on the env variable ROOT_INCLUDE_PATH."),
                  llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gCxxFlags("cxxflags", llvm::cl::ZeroOrMore,
          llvm::cl::desc("Extra C++ flags for the compiler."),
          llvm::cl::cat(gRootclingOptions));
static llvm::cl::opt<std::string>
gOptISysRoot("isysroot", llvm::cl::Prefix, llvm::cl::Hidden,
            llvm::cl::desc("Specify an isysroot."),
            llvm::cl::cat(gRootclingOptions),
            llvm::cl::init("-"));
static llvm::cl::list<std::string>
gOptIncludePaths("I", llvm::cl::Prefix, llvm::cl::ZeroOrMore,
                llvm::cl::desc("Specify an include path."),
                llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptPPDefines("D", llvm::cl::Prefix, llvm::cl::ZeroOrMore,
             llvm::cl::desc("Specify defined macros."),
             llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptPPUndefines("U", llvm::cl::Prefix, llvm::cl::ZeroOrMore,
             llvm::cl::desc("Specify undefined macros."),
             llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptWDiags("W", llvm::cl::Prefix, llvm::cl::ZeroOrMore,
          llvm::cl::desc("Specify compiler diagnostics options."),
          llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptDictionaryHeaderFiles(llvm::cl::Positional, llvm::cl::OneOrMore,
                         llvm::cl::desc("<list of dictionary header files> <LinkDef file>"),
                         llvm::cl::cat(gRootclingOptions));
static llvm::cl::list<std::string>
gOptSink(llvm::cl::ZeroOrMore, llvm::cl::Sink,
         llvm::cl::desc("Consumes all unrecognized options."),
         llvm::cl::cat(gRootclingOptions));

static llvm::cl::SubCommand
gBareClingSubcommand("bare-cling", "Call directly cling and exit.");

static llvm::cl::list<std::string>
gOptBareClingSink(llvm::cl::OneOrMore, llvm::cl::Sink,
                  llvm::cl::desc("Consumes options and sends them to cling."),
                  llvm::cl::cat(gRootclingOptions), llvm::cl::sub(gBareClingSubcommand));

////////////////////////////////////////////////////////////////////////////////
/// Returns true iff a given module (and its submodules) contains all headers
/// needed by the given ModuleGenerator.
/// The names of all header files that are needed by the ModuleGenerator but are
/// not in the given module will be inserted into the MissingHeader variable.
/// Returns true iff the PCH was successfully generated.
static bool ModuleContainsHeaders(TModuleGenerator &modGen, clang::Module *module,
                                  std::vector<std::string> &missingHeaders)
{
   // Now we collect all header files from the previously collected modules.
   std::vector<clang::Module::Header> moduleHeaders;
   TMetaUtils::foreachHeaderInModule(*module,
      [&moduleHeaders](const clang::Module::Header &h) { moduleHeaders.push_back(h); });

   bool foundAllHeaders = true;

   // Go through the list of headers that are required by the ModuleGenerator
   // and check for each header if it's in one of the modules we loaded.
   // If not, make sure we fail at the end and mark the header as missing.
   for (const std::string &header : modGen.GetHeaders()) {
      bool headerFound = false;
      for (const clang::Module::Header &moduleHeader : moduleHeaders) {
         if (header == moduleHeader.NameAsWritten) {
            headerFound = true;
            break;
         }
      }
      if (!headerFound) {
         missingHeaders.push_back(header);
         foundAllHeaders = false;
      }
   }
   return foundAllHeaders;
}

////////////////////////////////////////////////////////////////////////////////
/// Check moduleName validity from modulemap. Check if this module is defined or not.
static bool CheckModuleValid(TModuleGenerator &modGen, const std::string &resourceDir, cling::Interpreter &interpreter,
                           llvm::StringRef LinkdefPath, const std::string &moduleName)
{
#ifdef __APPLE__

   if (moduleName == "Krb5Auth" || moduleName == "GCocoa" || moduleName == "GQuartz")
      return true;
#endif

   clang::CompilerInstance *CI = interpreter.getCI();
   clang::HeaderSearch &headerSearch = CI->getPreprocessor().getHeaderSearchInfo();
   headerSearch.loadTopLevelSystemModules();

   // Actually lookup the module on the computed module name.
   clang::Module *module = headerSearch.lookupModule(llvm::StringRef(moduleName));

   // Inform the user and abort if we can't find a module with a given name.
   if (!module) {
      ::CppyyLegacy::TMetaUtils::Error("CheckModuleValid", "Couldn't find module with name '%s' in modulemap!\n",
                              moduleName.c_str());
      return false;
   }

   // Check if the loaded module covers all headers that were specified
   // by the user on the command line. This is an integrity check to
   // ensure that our used module map is
   std::vector<std::string> missingHeaders;
   if (!ModuleContainsHeaders(modGen, module, missingHeaders)) {
      // FIXME: Upgrade this to an error once modules are stable.
      std::stringstream msgStream;
      msgStream << "warning: Couldn't find in "
                << module->PresumedModuleMapFile
                << " the following specified headers in "
                << "the module " << module->Name << ":\n";
      for (auto &H : missingHeaders) {
         msgStream << "  " << H << "\n";
      }
      std::string warningMessage = msgStream.str();

      bool maybeUmbrella = modGen.GetHeaders().size() == 1;
      // We may have an umbrella and forgot to add the flag. Downgrade the
      // warning into an information message.
      // FIXME: We should open the umbrella, extract the set of header files
      // and check if they exist in the modulemap.
      // FIXME: We should also check if the header files are specified in the
      // modulemap file as they appeared in the rootcling invocation, i.e.
      // if we passed rootcling ... -I/some/path somedir/some/header, the
      // modulemap should contain module M { header "somedir/some/header" }
      // This way we will make sure the module is properly activated.
      if (!gOptUmbrellaInput && maybeUmbrella) {
         ::CppyyLegacy::TMetaUtils::Info("CheckModuleValid, %s. You can silence this message by adding %s to the invocation.",
                                warningMessage.c_str(),
                                gOptUmbrellaInput.ArgStr.data());
         return true;
      }

      ::CppyyLegacy::TMetaUtils::Warning("CheckModuleValid", warningMessage.c_str());
      // We include the missing headers to fix the module for the user.
      if (!IncludeHeaders(missingHeaders, interpreter)) {
         ::CppyyLegacy::TMetaUtils::Error("CheckModuleValid", "Couldn't include missing module headers for module '%s'!\n",
                                 module->Name.c_str());
      }
   }

   return true;
}


////////////////////////////////////////////////////////////////////////////////

int RootClingMain(int argc,
              char **argv,
              bool isGenreflex = false)
{
   // Set number of required arguments. We cannot do this globally since it
   // would interfere with LLVM's option parsing.
   gOptDictionaryFileName.setNumOccurrencesFlag(llvm::cl::Required);
   gOptDictionaryHeaderFiles.setNumOccurrencesFlag(llvm::cl::OneOrMore);

   // Copied from cling driver.
   // FIXME: Uncomment once we fix ROOT's teardown order.
   //llvm::llvm_shutdown_obj shutdownTrigger;

   const char *executableFileName = argv[0];

   llvm::sys::PrintStackTraceOnErrorSignal(executableFileName);
   llvm::PrettyStackTraceProgram X(argc, argv);
   MaybeSuppressWin32CrashDialogs();

#if defined(R__WIN32) && !defined(R__WINGCC)
   // FIXME: This is terrible hack allocating and changing the argument set.
   // We should remove it and use standard llvm facilities to convert the paths.
   // cygwin's make is presenting us some cygwin paths even though
   // we are windows native. Convert them as good as we can.
   for (int iic = 1 /* ignore binary file name in argv[0] */; iic < argc; ++iic) {
      std::string iiarg(argv[iic]);
      if (FromCygToNativePath(iiarg)) {
         size_t len = iiarg.length();
         // yes, we leak.
         char *argviic = new char[len + 1];
         strlcpy(argviic, iiarg.c_str(), len + 1);
         argv[iic] = argviic;
      }
   }
#endif

   // Hide options from llvm which we got from static initialization of libCling.
   llvm::cl::HideUnrelatedOptions(/*keep*/gRootclingOptions);

   // Define Options aliasses
   auto &opts = llvm::cl::getRegisteredOptions();
   llvm::cl::Option* optHelp = opts["help"];
   llvm::cl::alias optHelpAlias1("h",
                      llvm::cl::desc("Alias for -help"),
                      llvm::cl::aliasopt(*optHelp));
   llvm::cl::alias optHelpAlias2("?",
                      llvm::cl::desc("Alias for -help"),
                      llvm::cl::aliasopt(*optHelp));

   llvm::cl::ParseCommandLineOptions(argc, argv, "rootcling");

   std::string llvmResourceDir = std::string(gDriverConfig->fTROOT__GetEtcDir()) + "/cling";
   if (gBareClingSubcommand) {
      std::vector<const char *> clingArgsC;
      clingArgsC.push_back(executableFileName);
      // Help cling finds its runtime (RuntimeUniverse.h and such).
      clingArgsC.push_back("-I");
      clingArgsC.push_back(gDriverConfig->fTROOT__GetEtcDir());

      //clingArgsC.push_back("-resource-dir");
      //clingArgsC.push_back(llvmResourceDir.c_str());

      for (const std::string& Opt : gOptBareClingSink)
         clingArgsC.push_back(Opt.c_str());

      auto interp = std::make_unique<cling::Interpreter>(clingArgsC.size(),
                                                          &clingArgsC[0],
                                                          llvmResourceDir.c_str());
      // FIXME: Diagnose when we have misspelled a flag. Currently we show no
      // diagnostic and report exit as success.
      return interp->getDiagnostics().hasFatalErrorOccurred();
   }

   std::string dictname;
   std::string dictpathname;

   if (!gDriverConfig->fBuildingROOTStage1) {
      if (gOptRootBuild) {
         // running rootcling as part of the ROOT build for ROOT libraries.
         gBuildingROOT = true;
      }
   }

   if (!gOptModuleMapFiles.empty() && !gOptCxxModule) {
      ::CppyyLegacy::TMetaUtils::Error("", "Option %s can be used only when option %s is specified.\n",
                              gOptModuleMapFiles.ArgStr.str().c_str(),
                              gOptCxxModule.ArgStr.str().c_str());
      std::cout << "\n";
      llvm::cl::PrintHelpMessage();
      return 1;
   }

   // Set the default verbosity
   TMetaUtils::GetErrorIgnoreLevel() = gOptVerboseLevel;
   if (gOptVerboseLevel == v4)
      genreflex::verbose = true;

   if (gOptReflex)
      isGenreflex = true;

#if ROOT_VERSION_CODE < ROOT_VERSION(6,21,00)
   if (gOptCint)
      fprintf(stderr, "warning: Please remove the deprecated flag -cint.\n");
   if (gOptGccXml)
      fprintf(stderr, "warning: Please remove the deprecated flag -gccxml.\n");
   if (gOptC)
      fprintf(stderr, "warning: Please remove the deprecated flag -c.\n");
   if (gOptIgnoreExistingDict)
      fprintf(stderr, "warning: Please remove the deprecated flag -r.\n");

   for (auto I = gOptDictionaryHeaderFiles.begin(), E = gOptDictionaryHeaderFiles.end(); I != E; ++I) {
      if ((*I)[0] == '+') {
         // Mostly for +P, +V, +STUB which are legacy CINT flags.
         fprintf(stderr, "warning: Please remove the deprecated flag %s\n", I->c_str());
         // Remove it from the list because it will mess up our header input.
         gOptDictionaryHeaderFiles.erase(I);
      }
   }

   //for (const std::string& Opt : gOptSink)
      //fprintf(stderr, "warning: Please remove the deprecated flag %s\n", Opt.c_str());
#else
# error "Remove this deprecated code"
#endif

   if (!gOptLibListPrefix.empty()) {
      string filein = gOptLibListPrefix + ".in";
      FILE *fp;
      if ((fp = fopen(filein.c_str(), "r")) == 0) {
         ::CppyyLegacy::TMetaUtils::Error(0, "%s: The input list file %s does not exist\n", executableFileName, filein.c_str());
         return 1;
      }
      fclose(fp);
   }

   if (IsImplementationName(gOptDictionaryFileName)) {
      FILE *fp;
      if (!gOptIgnoreExistingDict && (fp = fopen(gOptDictionaryFileName.c_str(), "r")) != 0) {
         fclose(fp);
         if (!gOptForce) {
            ::CppyyLegacy::TMetaUtils::Error(0, "%s: output file %s already exists\n", executableFileName, gOptDictionaryFileName.c_str());
            return 1;
         }
      }

      // remove possible pathname to get the dictionary name
      if (gOptDictionaryFileName.size() > (PATH_MAX - 1)) {
         ::CppyyLegacy::TMetaUtils::Error(0, "rootcling: dictionary name too long (more than %d characters): %s\n",
                                 (PATH_MAX - 1), gOptDictionaryFileName.c_str());
         return 1;
      }

      dictpathname = gOptDictionaryFileName;
      dictname = llvm::sys::path::filename(gOptDictionaryFileName).str();
   }

   if (gOptForce && dictname.empty()) {
      ::CppyyLegacy::TMetaUtils::Error(0, "Inconsistent set of arguments detected: overwrite of dictionary file forced but no filename specified.\n");
      fprintf(stderr, "%s\n", shortHelp);
      return 1;
   }

   std::vector<std::string> clingArgs;
   clingArgs.push_back(executableFileName);
   clingArgs.push_back("-iquote.");

   bool dictSelection = !gOptNoDictSelection;

   // Collect the diagnostic pragmas linked to the usage of -W
   // Workaround for ROOT-5656
   std::list<std::string> diagnosticPragmas = {"#pragma clang diagnostic ignored \"-Wdeprecated-declarations\""};

   if (gOptFailOnWarnings) {
      using namespace CppyyLegacy::TMetaUtils;
      // If warnings are disabled with the current verbosity settings, lower
      // it so that the user sees the warning that caused the failure.
      if (GetErrorIgnoreLevel() > kWarning)
         GetErrorIgnoreLevel() = kWarning;
      GetWarningsAreErrors() = true;
   }

   if (gOptISysRoot != "-") {
      if (gOptISysRoot.empty()) {
        ::CppyyLegacy::TMetaUtils::Error("", "isysroot specified without a value.\n");
        return 1;
      }
      clingArgs.push_back(gOptISysRoot.ArgStr.str());
      clingArgs.push_back(gOptISysRoot.ValueStr.str());
   }

   // the "unknown options" are actually user additions to cling :P
   for (const std::string& Opt : gOptSink)
      clingArgs.push_back(Opt);

   // extra cxx flags provided
   for (const std::string& Opt : gCxxFlags) {
      std::istringstream splitter(Opt);
      std::copy(std::istream_iterator<std::string>(splitter),
         std::istream_iterator<std::string>(), std::back_inserter(clingArgs));
   }

   // Check if we have a multi dict request but no target library
   if (gOptMultiDict && gOptSharedLibFileName.empty()) {
      ::CppyyLegacy::TMetaUtils::Error("", "Multidict requested but no target library. Please specify one with the -s argument.\n");
      return 1;
   }

   for (const std::string &PPDefine : gOptPPDefines)
      clingArgs.push_back(std::string("-D") + PPDefine);

   for (const std::string &PPUndefine : gOptPPUndefines)
      clingArgs.push_back(std::string("-U") + PPUndefine);

   for (const std::string &IncludePath : gOptIncludePaths)
      clingArgs.push_back(std::string("-I") + llvm::sys::path::convert_to_slash(IncludePath));

   for (const std::string &WDiag : gOptWDiags) {
      const std::string FullWDiag = std::string("-W") + WDiag;
      // Suppress warning when compiling the dictionary, eg. gcc G__xxx.cxx
      CheckForMinusW(FullWDiag, diagnosticPragmas);
      // Suppress warning when compiling the input headers by cling.
      clingArgs.push_back(FullWDiag);
   }

   std::string includeDir = llvm::sys::path::convert_to_slash(gDriverConfig->fTROOT__GetIncludeDir());
   clingArgs.push_back(std::string("-I") + includeDir);

   std::vector<std::string> pcmArgs;
   for (size_t parg = 0, n = clingArgs.size(); parg < n; ++parg) {
      auto thisArg = clingArgs[parg];
      auto isInclude = TMetaUtils::BeginsWith(thisArg,"-I");
      if (thisArg == "-c" ||
          (gOptNoIncludePaths && isInclude)) continue;
      // We now check if the include directories are not excluded
      if (isInclude) {
         unsigned int offset = 2; // -I is two characters. Now account for spaces
         char c = thisArg[offset];
         while (c == ' ') c = thisArg[++offset];
         auto excludePathsEnd = gOptExcludePaths.end();
         auto excludePathPos = std::find_if(gOptExcludePaths.begin(),
                                            excludePathsEnd,
                                            [&](const std::string& path){
                                               return TMetaUtils::BeginsWith(&thisArg[offset], path);});
         if (excludePathsEnd != excludePathPos) continue;
      }
      pcmArgs.push_back(thisArg);
   }

   // cling-only arguments
   clingArgs.push_back(std::string("-I") + llvm::sys::path::convert_to_slash(gDriverConfig->fTROOT__GetEtcDir()));
   // We do not want __ROOTCLING__ in the pch!
   if (!gOptGeneratePCH) {
      clingArgs.push_back("-D__ROOTCLING__");
   }
#ifdef R__MACOSX
   clingArgs.push_back("-DSYSTEM_TYPE_macosx");
#elif defined(R__WIN32)
   clingArgs.push_back("-DSYSTEM_TYPE_winnt");

   // Prevent the following #error: The C++ Standard Library forbids macroizing keywords.
   clingArgs.push_back("-D_XKEYCHECK_H");
   // Tell windows.h not to #define min and max, it clashes with numerical_limits.
   clingArgs.push_back("-DNOMINMAX");
#else // assume UNIX
   clingArgs.push_back("-DSYSTEM_TYPE_unix");
#endif

   clingArgs.push_back("-fsyntax-only");
#ifndef R__WIN32
   clingArgs.push_back("-fPIC");
#endif
   clingArgs.push_back("-Xclang");
   clingArgs.push_back("-fmodules-embed-all-files");
   clingArgs.push_back("-Xclang");
   clingArgs.push_back("-main-file-name");
   clingArgs.push_back("-Xclang");
   clingArgs.push_back((dictname + ".h").c_str());

   TMetaUtils::SetPathsForRelocatability(clingArgs);

   // FIXME: This line is from TModuleGenerator, but we can't reuse this code
   // at this point because TModuleGenerator needs a CompilerInstance (and we
   // currently create the arguments for creating said CompilerInstance).
   bool isPCH = (dictpathname.rfind("allDict.cxx") != std::string::npos);
   std::string outputFile;
   // Data is in 'outputFile', therefore in the same scope.
   llvm::StringRef moduleName;
   std::string vfsArg;
   // Adding -fmodules to the args will break lexing with __CINT__ defined,
   // and we actually do lex with __CINT__ and reuse this variable later,
   // we have to copy it now.
   auto clingArgsInterpreter = clingArgs;

   if (gOptSharedLibFileName.empty()) {
      gOptSharedLibFileName = dictpathname;
   }

   if (!isPCH && gOptCxxModule) {
      // We just pass -fmodules, the CIFactory will do the rest and configure
      // clang correctly once it sees this flag.
      clingArgsInterpreter.push_back("-fmodules");
      clingArgsInterpreter.push_back("-fno-implicit-module-maps");

      for (const std::string &modulemap : gOptModuleMapFiles)
         clingArgsInterpreter.push_back("-fmodule-map-file=" + modulemap);

      clingArgsInterpreter.push_back("-fmodule-map-file=" +
                                     std::string(gDriverConfig->fTROOT__GetIncludeDir()) +
                                     "/module.modulemap");
      std::string ModuleMapCWD = FoundationUtils::GetCurrentDir() + "/module.modulemap";
      if (llvm::sys::fs::exists(ModuleMapCWD))
         clingArgsInterpreter.push_back("-fmodule-map-file=" + ModuleMapCWD);

      // Specify the module name that we can lookup the module in the modulemap.
      outputFile = llvm::sys::path::stem(gOptSharedLibFileName).str();
      // Try to get the module name in the modulemap based on the filepath.
      moduleName = llvm::sys::path::filename(outputFile);
      moduleName.consume_front("lib");
      moduleName.consume_back("_rdict.pcm");

      clingArgsInterpreter.push_back("-fmodule-name");
      clingArgsInterpreter.push_back(moduleName.str());

      std::string moduleCachePath = llvm::sys::path::parent_path(gOptSharedLibFileName).str();
      // FIXME: This is a horrible workaround to fix the incremental builds.
      // The enumerated modules are built by clang impicitly based on #include of
      // a header which is contained within that module. The build system has
      // no way to track dependencies on them and trigger a rebuild.
      // A possible solution can be to disable completely the implicit build of
      // modules and each module to be built by rootcling. We need to teach
      // rootcling how to build modules with no IO support.
      if (moduleName == "CoreLegacy") {
         assert(gDriverConfig->fBuildingROOTStage1);
         remove((moduleCachePath + llvm::sys::path::get_separator() + "_Builtin_intrinsics.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "_Builtin_stddef_max_align_t.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "Cling_Runtime.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "Cling_Runtime_Extra.pcm").str().c_str());
#ifdef R__MACOSX
         remove((moduleCachePath + llvm::sys::path::get_separator() + "Darwin.pcm").str().c_str());
#else
         remove((moduleCachePath + llvm::sys::path::get_separator() + "libc.pcm").str().c_str());
#endif
         remove((moduleCachePath + llvm::sys::path::get_separator() + "std.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "cuda.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "ROOT_Config.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "ROOT_Rtypes.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "ROOT_Foundation_C.pcm").str().c_str());
         remove((moduleCachePath + llvm::sys::path::get_separator() + "ROOT_Foundation_Stage1_NoRTTI.pcm").str().c_str());
      }

      // Set the C++ modules output directory to the directory where we generate
      // the shared library.
      clingArgsInterpreter.push_back("-fmodules-cache-path=" + moduleCachePath);
   }

   if (gOptVerboseLevel == v4)
      clingArgsInterpreter.push_back("-v");

   // Convert arguments to a C array and check if they are sane
   std::vector<const char *> clingArgsC;
   for (auto const &clingArg : clingArgsInterpreter) {
      if (!IsCorrectClingArgument(clingArg)){
         std::cerr << "Argument \""<< clingArg << "\" is not a supported cling argument. "
                   << "This could be mistyped rootcling argument. Please check the commandline.\n";
         return 1;
      }
      clingArgsC.push_back(clingArg.c_str());
   }


   std::unique_ptr<cling::Interpreter> owningInterpPtr;
   cling::Interpreter* interpPtr = nullptr;

   std::list<std::string> filesIncludedByLinkdef;
   if (!gDriverConfig->fBuildingROOTStage1) {
      // Pass the interpreter arguments to TCling's interpreter:
      clingArgsC.push_back("-resource-dir");
      clingArgsC.push_back(llvmResourceDir.c_str());
      clingArgsC.push_back(0); // signal end of array
      const char ** &extraArgs = *gDriverConfig->fTROOT__GetExtraInterpreterArgs();
      extraArgs = &clingArgsC[1]; // skip binary name
      interpPtr = gDriverConfig->fTCling__GetInterpreter();
      if (!isGenreflex && !gOptGeneratePCH) {
         std::unique_ptr<TRootClingCallbacks> callBacks (new TRootClingCallbacks(interpPtr, filesIncludedByLinkdef));
         interpPtr->setCallbacks(std::move(callBacks));
      }
   } else {
//#ifdef R__FAST_MATH
//      // Same setting as in TCling.cxx.
//      clingArgsC.push_back("-ffast-math");
//#endif

      owningInterpPtr.reset(new cling::Interpreter(clingArgsC.size(), &clingArgsC[0],
                                                   llvmResourceDir.c_str()));
      interpPtr = owningInterpPtr.get();
      // Force generation of _Builtin_intrinsics by rootcling_stage1.
      // The rest of the modules are implicitly generated by cling when including
      // RuntimeUniverse.h (via <new>).
      // FIXME: This should really go in the build system as for the rest of the
      // implicitly created modules.
      if (gOptCxxModule)
         interpPtr->loadModule("_Builtin_intrinsics", /*Complain*/ true);
   }
   cling::Interpreter &interp = *interpPtr;
   clang::CompilerInstance *CI = interp.getCI();
   // FIXME: Remove this once we switch cling to use the driver. This would handle  -fmodules-embed-all-files for us.
   CI->getFrontendOpts().ModulesEmbedAllFiles = true;
   CI->getSourceManager().setAllFilesAreTransient(true);

   clang::Preprocessor &PP = CI->getPreprocessor();
   clang::HeaderSearch &headerSearch = PP.getHeaderSearchInfo();
   clang::ModuleMap &moduleMap = headerSearch.getModuleMap();
   auto &diags = interp.getDiagnostics();

   // Manually enable the module build remarks. We don't enable them via the
   // normal clang command line arg because otherwise we would get remarks for
   // building STL/libc when starting the interpreter in rootcling_stage1.
   // We can't prevent these diags in any other way because we can only attach
   // our own diag client now after the interpreter has already started.
   diags.setSeverity(clang::diag::remark_module_build, clang::diag::Severity::Remark, clang::SourceLocation());

   // Attach our own diag client that listens to the module_build remarks from
   // clang to check that we don't build dictionary C++ modules implicitly.
   auto recordingClient = new CheckModuleBuildClient(diags.getClient(), diags.ownsClient(), moduleMap);
   diags.setClient(recordingClient, true);

   if (TMetaUtils::GetErrorIgnoreLevel() == TMetaUtils::kInfo) {
      ::CppyyLegacy::TMetaUtils::Info(0, "\n");
      ::CppyyLegacy::TMetaUtils::Info(0, "==== INTERPRETER CONFIGURATION ====\n");
      ::CppyyLegacy::TMetaUtils::Info(0, "== Include paths\n");
      interp.DumpIncludePath();
      printf("\n\n");
      fflush(stdout);

      ::CppyyLegacy::TMetaUtils::Info(0, "== Included files\n");
      interp.printIncludedFiles(llvm::outs());
      llvm::outs() << "\n\n";
      llvm::outs().flush();

      ::CppyyLegacy::TMetaUtils::Info(0, "== Language Options\n");
      const clang::LangOptions& LangOpts
         = interp.getCI()->getASTContext().getLangOpts();
#define LANGOPT(Name, Bits, Default, Description) \
      ::CppyyLegacy::TMetaUtils::Info(0, "%s = %d // %s\n", #Name, (int)LangOpts.Name, Description);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"
      ::CppyyLegacy::TMetaUtils::Info(0, "==== END interpreter configuration ====\n\n");
   }

   interp.getOptions().ErrorOut = true;
   interp.enableRawInput(true);
   if (!isGenreflex) {
      // rootcling
      if (interp.declare("#include <assert.h>\n"
                         "#include <stdlib.h>\n"
                         "#include <stddef.h>\n"
                         "#include <string.h>\n"
                        ) != cling::Interpreter::kSuccess
         || interp.declare("#include \"Rtypes.h\"\n"
                           "#include \"TClingRuntime.h\"\n"
                           "#include \"TObject.h\""
                          ) != cling::Interpreter::kSuccess
         ) {
         // There was an error.
         ::CppyyLegacy::TMetaUtils::Error(0, "Error loading the default header files.\n");
         return 1;
      }
   }

   // For the list of 'opaque' typedef to also include string, we have to include it now.
   interp.declare("#include <string>");

   // We are now ready (enough is loaded) to init the list of opaque typedefs.
   TMetaUtils::TNormalizedCtxt normCtxt(interp.getLookupHelper());
   TMetaUtils::TClingLookupHelper helper(interp, normCtxt, 0, 0, nullptr);
   TClassEdit::Init(&helper);

   // Process externally passed arguments if present.
   llvm::Optional<std::string> EnvOpt = llvm::sys::Process::GetEnv("EXTRA_CLING_ARGS");
   if (EnvOpt.has_value()) {
      llvm::StringRef Env(*EnvOpt);
      while (!Env.empty()) {
         llvm::StringRef Arg;
         std::tie(Arg, Env) = Env.split(' ');
         clingArgs.push_back(Arg.str());
      }
   }

   // flags used only for the pragma parser:
   clingArgs.push_back("-D__CINT__");
   clingArgs.push_back("-D__MAKECINT__");

   AddPlatformDefines(clingArgs);

   std::string currentDirectory = FoundationUtils::GetCurrentDir();

   std::string interpPragmaSource;
   std::string includeForSource;
   std::string interpreterDeclarations;
   std::string linkdef;

   for (size_t i = 0, e = gOptDictionaryHeaderFiles.size(); i < e; ++i) {
      const std::string& optHeaderFileName = gOptDictionaryHeaderFiles[i];
      bool isSelectionFile = IsSelectionFile(optHeaderFileName.c_str());

      if (isSelectionFile) {
         if (i == e - 1) {
            linkdef = optHeaderFileName;
         } else { // if the linkdef was not last, issue an error.
            ::CppyyLegacy::TMetaUtils::Error(0, "%s: %s must be last file on command line\n",
                                    executableFileName, optHeaderFileName.c_str());
            return 1;
         }
      }

      // coverity[tainted_data] The OS should already limit the argument size, so we are safe here
      std::string fullheader(optHeaderFileName);
      // Strip any trailing + which is only used by GeneratedLinkdef.h which currently
      // use directly argv.
      if (fullheader[fullheader.length() - 1] == '+') {
         fullheader.erase(fullheader.length() - 1);
      }
      std::string header(
         isSelectionFile ? fullheader : FoundationUtils::MakePathRelative(fullheader, currentDirectory, gBuildingROOT));

      interpPragmaSource += std::string("#include \"") + header + "\"\n";
      if (!isSelectionFile) {
         // In order to not have to add the equivalent to -I${PWD} to the
         // command line, include the complete file name, even if it is a
         // full pathname, when we write it down in the dictionary.
         // Note: have -I${PWD} means in that (at least in the case of
         // ACLiC) we inadvertently pick local file that have the same
         // name as system header (e.g. new or list) and -iquote has not
         // equivalent on some platforms.
         includeForSource += std::string("#include \"") + fullheader + "\"\n";
         pcmArgs.push_back(header);
      } else if (!IsSelectionXml(optHeaderFileName.c_str())) {
         interpreterDeclarations += std::string("#include \"") + header + "\"\n";
      }
   }

   if (gOptUmbrellaInput) {
      bool hasSelectionFile = !linkdef.empty();
      unsigned expectedHeaderFilesSize = 1 + hasSelectionFile;
      if (gOptDictionaryHeaderFiles.size() > expectedHeaderFilesSize)
         ::CppyyLegacy::TMetaUtils::Error(0, "Option %s used but more than one header file specified.\n",
                                 gOptUmbrellaInput.ArgStr.data());
   }


   if (gDriverConfig->fAddAncestorPCMROOTFile) {
      for (const auto & baseModule : gOptModuleDependencies)
         gDriverConfig->fAddAncestorPCMROOTFile(baseModule.c_str());
   }

   // We have a multiDict request. This implies generating a pcm which is of the form
   // dictName_libname_rdict.pcm
   if (gOptMultiDict) {

      std::string newName = llvm::sys::path::parent_path(gOptSharedLibFileName).str();
      if (!newName.empty())
         newName += gPathSeparator;
      newName += llvm::sys::path::stem(gOptSharedLibFileName);
      newName += "_";
      newName += llvm::sys::path::stem(dictpathname);
      newName += llvm::sys::path::extension(gOptSharedLibFileName);
      gOptSharedLibFileName = newName;
   }

   // Until the module are actually enabled in ROOT, we need to register
   // the 'current' directory to make it relocatable (i.e. have a way
   // to find the headers).
   if (!gBuildingROOT && !gOptNoIncludePaths){
      string incCurDir = "-I";
      incCurDir += currentDirectory;
      pcmArgs.push_back(incCurDir);
   }

   // Add the diagnostic pragmas distilled from the -Wno-xyz
   {
      std::stringstream res;
      const char* delim="\n";
      std::copy(diagnosticPragmas.begin(),
                diagnosticPragmas.end(),
                std::ostream_iterator<std::string>(res, delim));
      interp.declare(res.str());
   }

   class IgnoringPragmaHandler: public clang::PragmaNamespace {
   public:
      IgnoringPragmaHandler(const char* pragma):
         clang::PragmaNamespace(pragma) {}
      void HandlePragma(clang::Preprocessor &PP,
                        clang::PragmaIntroducer Introducer,
                        clang::Token &tok) override {
         PP.DiscardUntilEndOfDirective();
      }
   };

   // Ignore these #pragmas to suppress "unknown pragma" warnings.
   // See LinkdefReader.cxx.
   PP.AddPragmaHandler(new IgnoringPragmaHandler("link"));
   PP.AddPragmaHandler(new IgnoringPragmaHandler("extra_include"));
   PP.AddPragmaHandler(new IgnoringPragmaHandler("read"));
   PP.AddPragmaHandler(new IgnoringPragmaHandler("create"));

   if (!interpreterDeclarations.empty() &&
       interp.declare(interpreterDeclarations) != cling::Interpreter::kSuccess) {
      ::CppyyLegacy::TMetaUtils::Error(0, "%s: Linkdef compilation failure\n", executableFileName);
      return 1;
   }


   TModuleGenerator modGen(interp.getCI(),
                           gOptInlineInput,
                           gOptSharedLibFileName,
                           gOptWriteEmptyRootPCM);

   if (!gDriverConfig->fBuildingROOTStage1 && !filesIncludedByLinkdef.empty()) {
      pcmArgs.push_back(linkdef);
   }

   modGen.ParseArgs(pcmArgs);

   if (!gDriverConfig->fBuildingROOTStage1) {
      // Forward the -I, -D, -U
      for (const std::string & inclPath : modGen.GetIncludePaths()) {
         interp.AddIncludePath(inclPath);
      }
      std::stringstream definesUndefinesStr;
      modGen.WritePPDefines(definesUndefinesStr);
      modGen.WritePPUndefines(definesUndefinesStr);
      if (!definesUndefinesStr.str().empty())
         interp.declare(definesUndefinesStr.str());
   }

   if (!InjectModuleUtilHeader(executableFileName, modGen, interp, true)
         || !InjectModuleUtilHeader(executableFileName, modGen, interp, false)) {
      return 1;
   }

   if (linkdef.empty()) {
      // Generate autolinkdef
      GenerateLinkdef(gOptDictionaryHeaderFiles, interpPragmaSource);
   }

   // Check if code goes to stdout or rootcling file
   std::ofstream fileout;
   string main_dictname(dictpathname);
   std::ostream *dictStreamPtr = NULL;
   // Store the temp files
   tempFileNamesCatalog tmpCatalog;
   if (!gOptIgnoreExistingDict) {
      if (!dictpathname.empty()) {
         tmpCatalog.addFileName(dictpathname);
         fileout.open(dictpathname.c_str());
         dictStreamPtr = &fileout;
         if (!(*dictStreamPtr)) {
            ::CppyyLegacy::TMetaUtils::Error(0, "rootcling: failed to open %s in main\n",
                                    dictpathname.c_str());
            return 1;
         }
      } else {
         dictStreamPtr = &std::cout;
      }
   } else {
      fileout.open("/dev/null");
      dictStreamPtr = &fileout;
   }

   // Now generate a second stream for the split dictionary if it is necessary
   std::ostream *splitDictStreamPtr = gOptSplit ? CreateStreamPtrForSplitDict(dictpathname, tmpCatalog) : dictStreamPtr;
   std::ostream &dictStream = *dictStreamPtr;
   std::ostream &splitDictStream = *splitDictStreamPtr;

   size_t dh = main_dictname.rfind('.');
   if (dh != std::string::npos) {
      main_dictname.erase(dh);
   }
   // Need to replace all the characters not allowed in a symbol ...
   std::string main_dictname_copy(main_dictname);
   TMetaUtils::GetCppName(main_dictname, main_dictname_copy.c_str());

   CreateDictHeader(dictStream, main_dictname);
   if (gOptSplit)
      CreateDictHeader(splitDictStream, main_dictname);

   //---------------------------------------------------------------------------
   // Parse the linkdef or selection.xml file.
   /////////////////////////////////////////////////////////////////////////////

   string linkdefFilename;
   if (linkdef.empty()) {
      linkdefFilename = "in memory";
   } else {
      bool found = Which(interp, linkdef.c_str(), linkdefFilename);
      if (!found) {
         ::CppyyLegacy::TMetaUtils::Error(0, "%s: cannot open linkdef file %s\n", executableFileName, linkdef.c_str());
         return 1;
      }
   }

   // Exclude string not to re-generate the dictionary
   std::vector<std::pair<std::string, std::string>> namesForExclusion;
   if (!gBuildingROOT) {
      namesForExclusion.push_back(std::make_pair(TMetaUtils::propNames::name, "std::string"));
      namesForExclusion.push_back(std::make_pair(TMetaUtils::propNames::pattern, "Meta::Selection*"));
   }

   SelectionRules selectionRules(interp, normCtxt, namesForExclusion);

   std::string extraIncludes;

   TMetaUtils::RConstructorTypes constructorTypes;

   // Select using DictSelection
   const unsigned int selRulesInitialSize = selectionRules.Size();
   if (dictSelection && !gOptGeneratePCH)
      Internal::DictSelectionReader dictSelReader(interp, selectionRules, CI->getASTContext(), normCtxt);

   bool dictSelRulesPresent = selectionRules.Size() > selRulesInitialSize;

   bool isSelXML = IsSelectionXml(linkdefFilename.c_str());

   int rootclingRetCode(0);

   if (linkdef.empty()) {
      // There is no linkdef file, we added the 'default' #pragma to
      // interpPragmaSource.

      LinkdefReader ldefr(interp, constructorTypes);
      clingArgs.push_back("-Ietc/cling/cint"); // For multiset and multimap

      if (!ldefr.Parse(selectionRules, interpPragmaSource, clingArgs,
                       llvmResourceDir.c_str())) {
         ::CppyyLegacy::TMetaUtils::Error(0, "Parsing #pragma failed %s\n", linkdefFilename.c_str());
         rootclingRetCode += 1;
      } else {
         ::CppyyLegacy::TMetaUtils::Info(0, "#pragma successfully parsed.\n");
      }

      if (!ldefr.LoadIncludes(extraIncludes)) {
         ::CppyyLegacy::TMetaUtils::Error(0, "Error loading the #pragma extra_include.\n");
         return 1;
      }

   } else if (isSelXML) {

      selectionRules.SetSelectionFileType(SelectionRules::kSelectionXMLFile);

      std::ifstream file(linkdefFilename.c_str());
      if (file.is_open()) {
         ::CppyyLegacy::TMetaUtils::Info(0, "Selection XML file\n");

         XMLReader xmlr(interp);
         if (!xmlr.Parse(linkdefFilename.c_str(), selectionRules)) {
            ::CppyyLegacy::TMetaUtils::Error(0, "Parsing XML file %s\n", linkdefFilename.c_str());
            return 1; // Return here to propagate the failure up to the build system
         } else {
            ::CppyyLegacy::TMetaUtils::Info(0, "XML file successfully parsed\n");
         }
         file.close();
      } else {
         ::CppyyLegacy::TMetaUtils::Error(0, "XML file %s couldn't be opened!\n", linkdefFilename.c_str());
      }

   } else if (TMetaUtils::IsLinkdefFile(linkdefFilename.c_str())) {

      std::ifstream file(linkdefFilename.c_str());
      if (file.is_open()) {
         ::CppyyLegacy::TMetaUtils::Info(0, "Using linkdef file: %s\n", linkdefFilename.c_str());
         file.close();
      } else {
         ::CppyyLegacy::TMetaUtils::Error(0, "Linkdef file %s couldn't be opened!\n", linkdefFilename.c_str());
      }

      selectionRules.SetSelectionFileType(SelectionRules::kLinkdefFile);

      LinkdefReader ldefr(interp, constructorTypes);
      clingArgs.push_back("-Ietc/cling/cint"); // For multiset and multimap

      if (!ldefr.Parse(selectionRules, interpPragmaSource, clingArgs,
                       llvmResourceDir.c_str())) {
         ::CppyyLegacy::TMetaUtils::Error(0, "Parsing Linkdef file %s\n", linkdefFilename.c_str());
         rootclingRetCode += 1;
      } else {
         ::CppyyLegacy::TMetaUtils::Info(0, "Linkdef file successfully parsed.\n");
      }

      if (! ldefr.LoadIncludes(extraIncludes)) {
         ::CppyyLegacy::TMetaUtils::Error(0, "Error loading the #pragma extra_include.\n");
         return 1;
      }

   } else {

      ::CppyyLegacy::TMetaUtils::Error(0, "Unrecognized selection file: %s\n", linkdefFilename.c_str());

   }

   // Speed up the operations with rules
   selectionRules.FillCache();
   selectionRules.Optimize();

   // If we want to validate the selection only, we just quit.
   if (gOptCheckSelectionSyntax)
      return 0;

   //---------------------------------------------------------------------------
   // Write schema evolution related headers and declarations
   /////////////////////////////////////////////////////////////////////////////

   selectionRules.SearchNames(interp);

   int scannerVerbLevel = 0;
   {
      using namespace CppyyLegacy::TMetaUtils;
      scannerVerbLevel = GetErrorIgnoreLevel() == kInfo; // 1 if true, 0 if false
      if (isGenreflex){
         scannerVerbLevel = GetErrorIgnoreLevel() < kWarning;
      }
   }

   // Select the type of scan
   auto scanType = RScanner::EScanType::kNormal;
   if (gOptGeneratePCH)
      scanType = RScanner::EScanType::kOnePCM;
   if (dictSelection)
      scanType = RScanner::EScanType::kTwoPasses;

   RScanner scan(selectionRules,
                 scanType,
                 interp,
                 normCtxt,
                 scannerVerbLevel);

   // If needed initialize the autoloading hook
   if (!gOptLibListPrefix.empty()) {
      LoadLibraryMap(gOptLibListPrefix + ".in", gAutoloads);
      scan.SetRecordDeclCallback(RecordDeclCallback);
   }

   scan.Scan(CI->getASTContext());

   bool has_input_error = false;

   if (genreflex::verbose)
      selectionRules.PrintSelectionRules();

   if (TMetaUtils::GetErrorIgnoreLevel() != TMetaUtils::kFatal &&
         !gOptGeneratePCH &&
         !dictSelRulesPresent &&
         !selectionRules.AreAllSelectionRulesUsed()) {
      ::CppyyLegacy::TMetaUtils::Warning(0, "Not all selection rules are used!\n");
   }

   if (!gOptGeneratePCH){
      rootclingRetCode += CheckForUnsupportedClasses(scan.fSelectedClasses);
      if (rootclingRetCode) return rootclingRetCode;
   }

   // SELECTION LOOP
   // Check for error in the class layout before doing anything else.
   for (auto const & annRcd : scan.fSelectedClasses) {
      if (TMetaUtils::ClassInfo__HasMethod(annRcd, "Streamer", interp)) {
         if (annRcd.RequestNoInputOperator()) {
            int version = TMetaUtils::GetClassVersion(annRcd, interp);
            if (version != 0) {
               // Only Check for input operator is the object is I/O has
               // been requested.
               has_input_error |= CheckInputOperator(annRcd, interp);
            }
         }
      }
      has_input_error |= !CheckClassDef(*annRcd, interp);
   }

   if (has_input_error) {
      // Be a little bit makefile friendly and remove the dictionary in case of error.
      // We could add an option -k to keep the file even in case of error.
      exit(1);
   }

   //---------------------------------------------------------------------------
   // Write all the necessary #include
   /////////////////////////////////////////////////////////////////////////////
   if (!gDriverConfig->fBuildingROOTStage1) {
      for (auto &&includedFromLinkdef : filesIncludedByLinkdef) {
         includeForSource += "#include \"" + includedFromLinkdef + "\"\n";
      }
   }

   if (!gOptGeneratePCH) {
      GenerateNecessaryIncludes(dictStream, includeForSource, extraIncludes);
      if (gOptSplit) {
         GenerateNecessaryIncludes(splitDictStream, includeForSource, extraIncludes);
      }
      if (gDriverConfig->fInitializeStreamerInfoROOTFile) {
         gDriverConfig->fInitializeStreamerInfoROOTFile(modGen.GetModuleFileName().c_str());
      }

      // The order of addition to the list of constructor type
      // is significant.  The list is sorted by with the highest
      // priority first.
      if (!gOptInterpreterOnly) {
         constructorTypes.push_back(TMetaUtils::RConstructorType("__void__", interp)); // ROOT-7723
         constructorTypes.push_back(TMetaUtils::RConstructorType("", interp));
      }
   }

   if (gOptGeneratePCH) {
      AnnotateAllDeclsForPCH(interp, scan);
   } else if (gOptInterpreterOnly) {
      rootclingRetCode += CheckClassesForInterpreterOnlyDicts(interp, scan);
      // generate an empty pcm nevertheless for consistency
      // Negate as true is 1 and true is returned in case of success.
      if (!gDriverConfig->fBuildingROOTStage1) {
         rootclingRetCode +=  FinalizeStreamerInfoWriting(interp);
      }
   } else {
      rootclingRetCode += GenerateFullDict(splitDictStream,
                                 interp,
                                 scan,
                                 constructorTypes,
                                 gOptSplit,
                                 isGenreflex,
                                 gOptWriteEmptyRootPCM);
   }

   if (rootclingRetCode != 0) {
      return rootclingRetCode;
   }

   if (gOptSplit && splitDictStreamPtr) delete splitDictStreamPtr;

   // Now we have done all our looping and thus all the possible
   // annotation, let's write the pcms.
   HeadersDeclsMap_t headersClassesMap;
   HeadersDeclsMap_t headersDeclsMap;
   if (!gOptIgnoreExistingDict) {
      const std::string fwdDeclnArgsToKeepString(GetFwdDeclnArgsToKeepString(normCtxt, interp));

      ExtractHeadersForDecls(scan.fSelectedClasses,
                             scan.fSelectedTypedefs,
                             scan.fSelectedFunctions,
                             scan.fSelectedVariables,
                             scan.fSelectedEnums,
                             headersClassesMap,
                             headersDeclsMap,
                             interp);

      std::string detectedUmbrella;
      for (auto & arg : pcmArgs) {
         if (gOptInlineInput && !TMetaUtils::IsLinkdefFile(arg.c_str()) && TMetaUtils::IsHeaderName(arg)) {
            detectedUmbrella = arg;
            break;
         }
      }

      if (gOptWriteEmptyRootPCM){
         headersDeclsMap.clear();
      }


      std::string headersClassesMapString = "\"\"";
      std::string fwdDeclsString = "\"\"";
      if (!gOptCxxModule) {
         headersClassesMapString = GenerateStringFromHeadersForClasses(headersDeclsMap,
                                                                       detectedUmbrella,
                                                                       true);
         if (!gDriverConfig->fBuildingROOTStage1) {
            if (!gOptWriteEmptyRootPCM)
               fwdDeclsString = GenerateFwdDeclString(scan, interp);
         }
      }
      modGen.WriteRegistrationSource(dictStream, fwdDeclnArgsToKeepString, headersClassesMapString, fwdDeclsString,
                                     extraIncludes, gOptCxxModule);
      // If we just want to inline the input header, we don't need
      // to generate any files.
      if (!gOptInlineInput) {
         // Write the module/PCH depending on what mode we are on
         if (modGen.IsPCH()) {
            if (!GenerateAllDict(modGen, CI, currentDirectory)) return 1;
         } else if (gOptCxxModule) {
            if (!CheckModuleValid(modGen, llvmResourceDir, interp, linkdefFilename, moduleName.str()))
               return 1;
         }
      }
   }


   if (!gOptLibListPrefix.empty()) {
      string liblist_filename = gOptLibListPrefix + ".out";

      ofstream outputfile(liblist_filename.c_str(), ios::out);
      if (!outputfile) {
         ::CppyyLegacy::TMetaUtils::Error(0, "%s: Unable to open output lib file %s\n",
                                 executableFileName, liblist_filename.c_str());
      } else {
         const size_t endStr = gLibsNeeded.find_last_not_of(" \t");
         outputfile << gLibsNeeded.substr(0, endStr + 1) << endl;
         // Add explicit delimiter
         outputfile << "# Now the list of classes\n";
         // SELECTION LOOP
         for (auto const & annRcd : scan.fSelectedClasses) {
            outputfile << annRcd.GetNormalizedName() << endl;
         }
      }
   }

   // Check for errors in module generation
   rootclingRetCode += modGen.GetErrorCount();
   if (0 != rootclingRetCode) return rootclingRetCode;

   // Create the rootmap file
   std::string rootmapLibName = std::accumulate(gOptRootmapLibNames.begin(),
                                gOptRootmapLibNames.end(),
                                std::string(),
   [](const std::string & a, const std::string & b) -> std::string {
      if (a.empty()) return b;
      else return a + " " + b;
   });

   bool rootMapNeeded = !gOptRootMapFileName.empty() || !rootmapLibName.empty();

   std::list<std::string> classesNames;
   std::list<std::string> classesNamesForRootmap;
   std::list<std::string> classesDefsList;

   rootclingRetCode = ExtractClassesListAndDeclLines(scan,
                                                     classesNames,
                                                     classesNamesForRootmap,
                                                     classesDefsList,
                                                     interp);

   std::list<std::string> enumNames;
   rootclingRetCode += ExtractAutoloadKeys(enumNames,
                                           scan.fSelectedEnums,
                                           interp);

   std::list<std::string> varNames;
   rootclingRetCode += ExtractAutoloadKeys(varNames,
                                           scan.fSelectedVariables,
                                           interp);

   if (0 != rootclingRetCode) return rootclingRetCode;

   // Create the rootmapfile if needed
   if (rootMapNeeded) {

      std::list<std::string> nsNames;

      ExtractSelectedNamespaces(scan, nsNames);

      AdjustRootMapNames(gOptRootMapFileName,
                         rootmapLibName);

      ::CppyyLegacy::TMetaUtils::Info(0, "Rootmap file name %s and lib name(s) \"%s\"\n",
                             gOptRootMapFileName.c_str(),
                             rootmapLibName.c_str());

      tmpCatalog.addFileName(gOptRootMapFileName);
      std::unordered_set<std::string> headersToIgnore;
      if (gOptInlineInput)
         for (const std::string& optHeaderFileName : gOptDictionaryHeaderFiles)
            headersToIgnore.insert(optHeaderFileName.c_str());

      std::list<std::string> typedefsRootmapLines;
      rootclingRetCode += ExtractAutoloadKeys(typedefsRootmapLines,
                                              scan.fSelectedTypedefs,
                                              interp);

      rootclingRetCode = CreateNewRootMapFile(gOptRootMapFileName,
                                          rootmapLibName,
                                          classesDefsList,
                                          classesNamesForRootmap,
                                          nsNames,
                                          typedefsRootmapLines,
                                          enumNames,
                                          varNames,
                                          headersClassesMap,
                                          headersToIgnore);

      if (0 != rootclingRetCode) return 1;
   }

   if (genreflex::verbose)
      tmpCatalog.dump();

   // Manually call end of translation unit because we never call the
   // appropriate deconstructors in the interpreter. This writes out the C++
   // module file that we currently generate.
   {
      cling::Interpreter::PushTransactionRAII RAII(&interp);
      CI->getSema().getASTConsumer().HandleTranslationUnit(CI->getSema().getASTContext());
   }

   // Add the warnings
   rootclingRetCode += TMetaUtils::GetNumberOfErrors();

   // make sure the file is closed before committing
   fileout.close();

   // Before returning, rename the files if no errors occurred
   // otherwise clean them to avoid remnants (see ROOT-10015)
   if(rootclingRetCode == 0) {
      rootclingRetCode += tmpCatalog.commit();
   } else {
      tmpCatalog.clean();
   }

   return rootclingRetCode;

}

namespace genreflex {

////////////////////////////////////////////////////////////////////////////////
/// Loop on arguments: stop at the first which starts with -

   unsigned int checkHeadersNames(std::vector<std::string> &headersNames)
   {
      unsigned int numberOfHeaders = 0;
      for (std::vector<std::string>::iterator it = headersNames.begin();
            it != headersNames.end(); ++it) {
         const std::string headername(*it);
         if (TMetaUtils::IsHeaderName(headername)) {
            numberOfHeaders++;
         } else {
            ::CppyyLegacy::TMetaUtils::Warning(0,
                                      "*** genreflex: %s is not a valid header name (.h and .hpp extensions expected)!\n",
                                      headername.c_str());
         }
      }
      return numberOfHeaders;
   }

////////////////////////////////////////////////////////////////////////////////
/// Extract the arguments from the command line

   unsigned int extractArgs(int argc, char **argv, std::vector<std::string> &args)
   {
      // loop on argv, spot strings which are not preceded by something
      unsigned int argvCounter = 0;
      for (int i = 1; i < argc; ++i) {
         if (!TMetaUtils::BeginsWith(argv[i - 1], "-") && // so, if preceding element starts with -, this is a value for an option
               !TMetaUtils::BeginsWith(argv[i], "-")) { // and the element itself is not an option
            args.push_back(argv[i]);
            argvCounter++;
         } else  if (argvCounter) {
            argv[i - argvCounter] = argv[i];
         }
      }

      // Some debug
      if (genreflex::verbose) {
         int i = 0;
         std::cout << "Args: \n";
         for (std::vector<std::string>::iterator it = args.begin();
               it < args.end(); ++it) {
            std::cout << i << ") " << *it << std::endl;
            ++i;
         }

      }

      return argvCounter;
   }

////////////////////////////////////////////////////////////////////////////////

   void changeExtension(std::string &filename, const std::string &newExtension)
   {
      size_t result = filename.find_last_of('.');
      if (std::string::npos != result) {
         filename.erase(result);
         filename.append(newExtension);
      }

   }

////////////////////////////////////////////////////////////////////////////////
/// The caller is responsible for deleting the string!

   char *string2charptr(const std::string &str)
   {
      const unsigned int size(str.size());
      char *a = new char[size + 1];
      a[size] = 0;
      memcpy(a, str.c_str(), size);
      return a;
   }

////////////////////////////////////////////////////////////////////////////////
/// Replace the extension with "_rflx.cpp"

   void header2outputName(std::string &fileName)
   {
      changeExtension(fileName, "_rflx.cpp");
   }

////////////////////////////////////////////////////////////////////////////////
/// Get a proper name for the output file

   void headers2outputsNames(const std::vector<std::string> &headersNames,
                             std::vector<std::string> &ofilesnames)
   {
      ofilesnames.reserve(headersNames.size());

      for (std::vector<std::string>::const_iterator it = headersNames.begin();
            it != headersNames.end(); ++it) {
         std::string ofilename(*it);
         header2outputName(ofilename);
         ofilesnames.push_back(ofilename);
      }
   }

////////////////////////////////////////////////////////////////////////////////

   void AddToArgVector(std::vector<char *> &argvVector,
                       const std::vector<std::string> &argsToBeAdded,
                       const std::string &optName = "")
   {
      for (std::vector<std::string>::const_iterator it = argsToBeAdded.begin();
            it != argsToBeAdded.end(); ++it) {
         argvVector.push_back(string2charptr(optName + *it));
      }
   }

////////////////////////////////////////////////////////////////////////////////

   void AddToArgVectorSplit(std::vector<char *> &argvVector,
                            const std::vector<std::string> &argsToBeAdded,
                            const std::string &optName = "")
   {
      for (std::vector<std::string>::const_iterator it = argsToBeAdded.begin();
            it != argsToBeAdded.end(); ++it) {
         if (optName.length()) {
            argvVector.push_back(string2charptr(optName));
         }
         argvVector.push_back(string2charptr(*it));
      }
   }

////////////////////////////////////////////////////////////////////////////////

   int invokeRootCling(const std::string &verbosity,
                       const std::string &selectionFileName,
                       const std::string &targetLibName,
                       bool multiDict,
                       const std::vector<std::string> &pcmsNames,
                       const std::vector<std::string> &includes,
                       const std::vector<std::string> &preprocDefines,
                       const std::vector<std::string> &preprocUndefines,
                       const std::vector<std::string> &warnings,
                       const std::string &rootmapFileName,
                       const std::string &rootmapLibName,
                       const std::string &extraFlags,
                       bool interpreteronly,
                       bool doSplit,
                       bool isCxxmodule,
                       bool writeEmptyRootPCM,
                       bool selSyntaxOnly,
                       bool noIncludePaths,
                       const std::vector<std::string> &headersNames,
                       bool failOnWarnings,
                       const std::string &ofilename)
   {
      // Prepare and invoke the commandline to invoke rootcling

      std::vector<char *> argvVector;

      argvVector.push_back(string2charptr("rootcling"));
      argvVector.push_back(string2charptr(verbosity));
      argvVector.push_back(string2charptr("-f"));
      argvVector.push_back(string2charptr(ofilename));

      if (isCxxmodule)
         argvVector.push_back(string2charptr("-cxxmodule"));

      // Extract the path to the dictionary
      std::string dictLocation;
      ExtractFilePath(ofilename, dictLocation);

      // Rootmaps

      // Prepare the correct rootmap libname if not already set.
      std::string newRootmapLibName(rootmapLibName);
      if (!rootmapFileName.empty() && newRootmapLibName.empty()) {
         if (headersNames.size() != 1) {
            ::CppyyLegacy::TMetaUtils::Warning(0,
                                      "*** genreflex: No rootmap lib and several header specified!\n");
         }
         std::string cleanHeaderName = ExtractFileName(headersNames[0]);
         newRootmapLibName = "lib";
         newRootmapLibName += cleanHeaderName;
         changeExtension(newRootmapLibName, gLibraryExtension);
      }

      // Prepend to the rootmap the designed directory of the dictionary
      // if no path is specified for the rootmap itself
      std::string newRootmapFileName(rootmapFileName);
      if (!newRootmapFileName.empty() && !HasPath(newRootmapFileName)) {
         newRootmapFileName = dictLocation + newRootmapFileName;
      }


      // RootMap filename
      if (!newRootmapFileName.empty()) {
         argvVector.push_back(string2charptr("-rmf"));
         argvVector.push_back(string2charptr(newRootmapFileName));
      }

      // RootMap Lib filename
      if (!newRootmapLibName.empty()) {
         argvVector.push_back(string2charptr("-rml"));
         argvVector.push_back(string2charptr(newRootmapLibName));
      }

      // any extra C++ flags picked up
      if (!extraFlags.empty()) {
         argvVector.push_back(string2charptr("-cxxflags"));
         argvVector.push_back(string2charptr(extraFlags));
      }

      // Interpreter only dictionaries
      if (interpreteronly)
         argvVector.push_back(string2charptr("-interpreteronly"));

      // Split dictionaries
      if (doSplit)
         argvVector.push_back(string2charptr("-split"));

      // Targetlib
      if (!targetLibName.empty()) {
         argvVector.push_back(string2charptr("-s"));
         argvVector.push_back(string2charptr(targetLibName));
      }

      // Multidict support
      if (multiDict)
         argvVector.push_back(string2charptr("-multiDict"));


      AddToArgVectorSplit(argvVector, pcmsNames, "-m");

      // Inline the input header
      // argvVector.push_back(string2charptr("-inlineInputHeader"));

      // Write empty root pcms
      if (writeEmptyRootPCM)
         argvVector.push_back(string2charptr("-writeEmptyRootPCM"));

      // Just test the syntax of the selection file
      if (selSyntaxOnly)
         argvVector.push_back(string2charptr("-selSyntaxOnly"));

      // No include paths
      if (noIncludePaths)
         argvVector.push_back(string2charptr("-noIncludePaths"));

      // Fail on warnings
      if (failOnWarnings)
         argvVector.push_back(string2charptr("-failOnWarnings"));

      // Clingargs
      AddToArgVector(argvVector, includes, "-I");
      AddToArgVector(argvVector, preprocDefines, "-D");
      AddToArgVector(argvVector, preprocUndefines, "-U");
      AddToArgVector(argvVector, warnings, "-W");

      AddToArgVector(argvVector, headersNames);

      if (!selectionFileName.empty()) {
         argvVector.push_back(string2charptr(selectionFileName));
      }

      const int argc = argvVector.size();

      // Output commandline for rootcling
      if (genreflex::verbose) {
         std::cout << "Rootcling commandline:\n";
         for (int i = 0; i < argc; i++)
            std::cout << i << ") " << argvVector[i] << std::endl;
      }

      char **argv =  & (argvVector[0]);
      int rootclingReturnCode = RootClingMain(argc,
                                              argv,
                                              /*isGenReflex=*/true);

      for (int i = 0; i < argc; i++)
         delete [] argvVector[i];

      return rootclingReturnCode;

   }

////////////////////////////////////////////////////////////////////////////////
/// Get the right ofilenames and invoke several times rootcling
/// One invokation per header

   int invokeManyRootCling(const std::string &verbosity,
                           const std::string &selectionFileName,
                           const std::string &targetLibName,
                           bool multiDict,
                           const std::vector<std::string> &pcmsNames,
                           const std::vector<std::string> &includes,
                           const std::vector<std::string> &preprocDefines,
                           const std::vector<std::string> &preprocUndefines,
                           const std::vector<std::string> &warnings,
                           const std::string &rootmapFileName,
                           const std::string &rootmapLibName,
                           const std::string &extraFlags,
                           bool interpreteronly,
                           bool doSplit,
                           bool isCxxmodule,
                           bool writeEmptyRootPCM,
                           bool selSyntaxOnly,
                           bool noIncludePaths,
                           const std::vector<std::string> &headersNames,
                           bool failOnWarnings,
                           const std::string &outputDirName_const = "")
   {
      std::string outputDirName(outputDirName_const);

      std::vector<std::string> ofilesNames;
      headers2outputsNames(headersNames, ofilesNames);

      if (!outputDirName.empty() && !TMetaUtils::EndsWith(outputDirName, gPathSeparator)) {
         outputDirName += gPathSeparator;
      }

      std::vector<std::string> namesSingleton(1);
      for (unsigned int i = 0; i < headersNames.size(); ++i) {
         namesSingleton[0] = headersNames[i];
         std::string ofilenameFullPath(ofilesNames[i]);
         if (llvm::sys::path::parent_path(ofilenameFullPath) == "")
            ofilenameFullPath = outputDirName + ofilenameFullPath;
         int returnCode = invokeRootCling(verbosity,
                                          selectionFileName,
                                          targetLibName,
                                          multiDict,
                                          pcmsNames,
                                          includes,
                                          preprocDefines,
                                          preprocUndefines,
                                          warnings,
                                          rootmapFileName,
                                          rootmapLibName,
                                          extraFlags,
                                          interpreteronly,
                                          doSplit,
                                          isCxxmodule,
                                          writeEmptyRootPCM,
                                          selSyntaxOnly,
                                          noIncludePaths,
                                          namesSingleton,
                                          failOnWarnings,
                                          ofilenameFullPath);
         if (returnCode != 0)
            return returnCode;
      }

      return 0;
   }


} // end genreflex namespace

////////////////////////////////////////////////////////////////////////////////
/// Extract from options multiple values with the same option

int extractMultipleOptions(std::vector<option::Option> &options,
                           int oIndex,
                           std::vector<std::string> &values)
{
   int nValues = 0;
   if (options[oIndex]) {
      const int nVals = options[oIndex].count();
      values.reserve(nVals);
      int optionIndex = 0;
      for (option::Option *opt = options[oIndex]; opt; opt = opt->next()) {
         if (genreflex::verbose) std::cout << "Extracting multiple args: "
                                              << optionIndex << "/" << nVals << " "
                                              << opt->arg << std::endl;
         optionIndex++;
         values.push_back(opt->arg);
         nValues++;
      }
   }
   return nValues;
}

////////////////////////////////////////////////////////////////////////////////

void RiseWarningIfPresent(std::vector<option::Option> &options,
                          int optionIndex,
                          const char *descriptor)
{
   if (options[optionIndex]) {
      ::CppyyLegacy::TMetaUtils::Warning(0,
                                "*** genereflex: %s is not supported anymore.\n",
                                descriptor);
   }
}

////////////////////////////////////////////////////////////////////////////////

bool IsGoodLibraryName(const std::string &name)
{


   auto isGood = CppyyLegacy::TMetaUtils::EndsWith(name, gLibraryExtension);
#ifdef __APPLE__
   isGood |= CppyyLegacy::TMetaUtils::EndsWith(name, ".dylib");
#endif
   return isGood;
}

////////////////////////////////////////////////////////////////////////////////
/// Translate the arguments of genreflex into rootcling ones and forward them
/// to the RootCling function.
/// These are two typical genreflex and rootcling commandlines
/// 1) genreflex header1.h [header2.h ...] [options] [preprocessor options]
/// 2) rootcling [-v] [-v0-4] [-f] [out.cxx] [-s sharedlib.so] [-m pcmfilename]
///     header1.h[{+,-}][!] ..headerN.h[{+,-}][!] [{LinkDef.h,selectionRules.xml}]
/// The rules with which the arguments are translated are (1st column genreflex):
/// --debug                             -v4
/// --quiet                             -v0
/// -o ofile                            positional arg after -f
/// -s selection file                   Last argument of the call
/// --fail_on_warning                   Wrap CppyyLegacy::TMetaUtils::Warning and throw if selected
///
/// New arguments:
/// -l --library targetLib name (new)   -s  targetLib name
/// -m pcmname (can be many -m) (new)   -m pcmname (can be many -m)
/// --rootmap                           -rmf (new)
/// --rootmap-lib                       -rml (new)
///
/// genreflex options which rise warnings (feedback is desirable)
/// --no_membertypedefs (it should be irrelevant)
/// --no_templatetypedefs (it should be irrelevant)
///
/// genreflex options which are ignored (know for sure they are not needed)
/// --pool, --dataonly
/// --interpreteronly
/// --gccxml{path,opt,post}
///
///
/// Exceptions
/// The --deep option of genreflex is passed as function parameter to rootcling
/// since it's not needed at the moment there.

int GenReflexMain(int argc, char **argv)
{
   using namespace genreflex;

   // Setup the options parser
   enum  optionIndex { UNKNOWN,
                       OFILENAME,
                       TARGETLIB,
                       MULTIDICT,
                       SELECTIONFILENAME,
                       ROOTMAP,
                       ROOTMAPLIB,
                       EXTRAFLAGS,
                       PCMFILENAME,
                       DEEP,
                       DEBUG,
                       VERBOSE,
                       QUIET,
                       SILENT,
                       CXXMODULE,
                       WRITEEMPTYROOTPCM,
                       HELP,
                       FAILONWARNINGS,
                       SELSYNTAXONLY,
                       INTERPRETERONLY,
                       SPLIT,
                       NOMEMBERTYPEDEFS,
                       NOTEMPLATETYPEDEFS,
                       NOINCLUDEPATHS,
                       // Don't show up in the help
                       PREPROCDEFINE,
                       PREPROCUNDEFINE,
                       INCLUDE,
                       WARNING
                     };

   enum  optionTypes { NOTYPE, STRING } ;

   // Some long help strings
   const char *genreflexUsage =
      "Generates dictionary sources and related ROOT pcm starting from an header.\n"
      "Usage: genreflex headerfile.h [opts] [preproc. opts]\n\n"
      "Options:\n";

   const char *selectionFilenameUsage =
      "-s, --selection_file\tSelection filename\n"
      "      Class selection file to specify for which classes the dictionary\n"
      "      will be generated. The final set can be crafted with exclusion and\n"
      "      exclusion rules.\n"
      "      Properties can be specified. Some have special meaning:\n"
      "      - name [string] name of the entity to select with an exact matching\n"
      "      - pattern [string] name with wildcards (*) to select entities\n"
      "      - file_name/file_pattern [string]: as name/pattern but referring to\n"
      "        file where the C++ entities reside and not to C++ entities themselves.\n"
      "      - transient/persistent [string: true/false] The fields to which they are\n"
      "        applied will not be persistified if requested.\n"
      "      - comment [string]: what you could write in code after an inline comment\n"
      "        without \"//\". For example comment=\"!\" or \"||\".\n"
      "      - noStreamer [true/false]: turns off streamer generation if set to 'true.'\n"
      "        Default value is 'false'\n"
      "      - noInputOperator [true/false]: turns off input operator generation if set\n"
      "        to 'true'. Default value is 'false'\n"
      "      Example XML:\n"
      "        <lcgdict>\n"
      "        [<selection>]\n"
      "          <class [name=\"classname\"] [pattern=\"wildname\"]\n"
      "                 [file_name=\"filename\"] [file_pattern=\"wildname\"]\n"
      "                 [id=\"xxxx\"] [noStreamer=\"true/false\"]\n"
      "                 [noInputOperator=\"true/false\"] />\n"
      "          <class name=\"classname\" >\n"
      "            <field name=\"m_transient\" transient=\"true\"/>\n"
      "            <field name=\"m_anothertransient\" persistent=\"false\"/>\n"
      "            <field name=\"m_anothertransient\" comment=\"||\"/>\n"
      "            <properties prop1=\"value1\" [prop2=\"value2\"]/>\n"
      "          </class>\n"
      "          <function [name=\"funcname\"] [pattern=\"wildname\"] />\n"
      "          <enum [name=\"enumname\"] [pattern=\"wildname\"] />\n"
      "          <variable [name=\"varname\"] [pattern=\"wildname\"] />\n"
      "        [</selection>]\n"
      "        <exclusion>\n"
      "          <class [name=\"classname\"] [pattern=\"wildname\"] />\n"
      "            <method name=\"unwanted\" />\n"
      "          </class>\n"
      "        ...\n"
      "        </lcgdict>\n"
      "\n"
      "      If no selection file is specified, the class with the filename without\n"
      "      extension will be selected, i.e. myClass.h as argument without any\n"
      "      selection xml comes with an implicit selection rule for class \"myClass\".\n";

   const char *outputFilenameUsage =
      "-o, --output\tOutput filename\n"
      "      Output file name. If an existing directory is specified instead of a file,\n"
      "      then a filename will be build using the name of the input file and will\n"
      "      be placed in the given directory. <headerfile>_rflx.cpp.\n"
      "      NOTA BENE: the dictionaries that will be used within the same project must\n"
      "      have unique names.\n";


   const char *targetLib =
      "-l, --library\tTarget library\n"
      "      The flag -l must be followed by the name of the library that will\n"
      "      contain the object file corresponding to the dictionary produced by\n"
      "      this invocation of genreflex.\n"
      "      The name takes priority over the one specified for the rootmapfile.\n"
      "      The name influences the name of the created pcm:\n"
      "       1) If it is not specified, the pcm is called libINPUTHEADER_rdict.pcm\n"
      "       2) If it is specified, the pcm is called libTARGETLIBRARY_rdict.pcm\n"
      "          Any \"liblib\" occurence is transformed in the expected \"lib\".\n"
      "       3) If this is specified in conjunction with --multiDict, the output is\n"
      "          libTARGETLIBRARY_DICTIONARY_rdict.pcm\n";

   const char *rootmapUsage =
      "--rootmap\tGenerate the rootmap file to be used by ROOT.\n"
      "      This file lists the autoload keys. For example classes for which the\n"
      "      reflection information is provided.\n"
      "      The format of the rootmap is the following:\n"
      "        - Forward declarations section\n"
      "        - Libraries sections\n"
      "      Rootmaps can be concatenated together, for example with the cat util.\n"
      "      In order for ROOT to pick up the information in the rootmaps, they\n"
      "      have to be located in the library path and have the .rootmap extension.\n"
      "      An example rootmap file could be:\n"
      "      { decls }\n"
      "      template <class T> class A;\n"
      "      [ libMyLib.so ]\n"
      "      class A<double>\n"
      "      class B\n"
      "      typedef C\n"
      "      header H.h\n";

   const char *rootmapLibUsage =
      "--rootmap-lib\tLibrary name for the rootmap file.\n";

   const char *cxxflagsUsage =
      "--cxxflags\textra C++ compiler option flags.\n";

   // The Descriptor
   const ::CppyyLegacy::option::Descriptor genreflexUsageDescriptor[] = {
      {
         UNKNOWN,
         NOTYPE,
         "", "",
         ::CppyyLegacy::option::Arg::None,
         genreflexUsage
      },

      {
         OFILENAME,
         STRING ,
         "o" , "output" ,
         ::CppyyLegacy::option::FullArg::Required,
         outputFilenameUsage
      },

      {
         TARGETLIB,
         STRING ,
         "l" , "library" ,
         ::CppyyLegacy::option::FullArg::Required,
         targetLib
      },

      {
         MULTIDICT,
         NOTYPE ,
         "" , "multiDict" ,
         ::CppyyLegacy::option::FullArg::None,
         "--multiDict\tSupport for many dictionaries in one library\n"
         "      Form correct pcm names if multiple dictionaries will be in the same\n"
         "      library (needs target library switch. See its documentation).\n"
      },

      {
         SELECTIONFILENAME,
         STRING ,
         "s" , "selection_file" ,
         ::CppyyLegacy::option::FullArg::Required,
         selectionFilenameUsage
      },

      {
         ROOTMAP,
         STRING ,
         "" , "rootmap" ,
         ::CppyyLegacy::option::FullArg::Required,
         rootmapUsage
      },

      {
         ROOTMAPLIB,
         STRING ,
         "" , "rootmap-lib" ,
         ::CppyyLegacy::option::FullArg::Required,
         rootmapLibUsage
      },

      {
         EXTRAFLAGS,
         STRING ,
         "", "cxxflags",
         ::CppyyLegacy::option::FullArg::Required,
         cxxflagsUsage
      }, 

      {
         INTERPRETERONLY,
         NOTYPE,
         "" , "interpreteronly",
         ::CppyyLegacy::option::Arg::None,
         "--interpreteronly\tDo not generate I/O related information.\n"
         "      Generate minimal dictionary required for interactivity.\n"
      },

      {
         SPLIT,
         NOTYPE,
         "" , "split",
         ::CppyyLegacy::option::Arg::None,
         "--split\tSplit the dictionary\n"
         "      Split in two the dictionary, isolating the part with\n"
         "      ClassDef related functions in a separate file.\n"
      },

      {
         PCMFILENAME,
         STRING ,
         "m" , "" ,
         ::CppyyLegacy::option::FullArg::Required,
         "-m \tPcm file loaded before any header (option can be repeated).\n"
      },

      {
         DEEP,  // Not active. Will be removed for 6.2
         NOTYPE ,
         "" , "deep",
         ::CppyyLegacy::option::Arg::None,
         ""
      },
      //"--deep\tGenerate dictionaries for all dependent classes (ignored).\n"

      {
         VERBOSE,
         NOTYPE ,
         "-v" , "verbose",
         ::CppyyLegacy::option::Arg::None,
         "-v, --verbose\tPrint some debug information.\n"
      },

      {
         DEBUG,
         NOTYPE ,
         "" , "debug",
         ::CppyyLegacy::option::Arg::None,
         "--debug\tPrint all debug information.\n"
      },

      {
         QUIET,
         NOTYPE ,
         "" , "quiet",
         ::CppyyLegacy::option::Arg::None,
         "--quiet\tPrint only warnings and errors (default).\n"
      },

      {
         SILENT,
         NOTYPE ,
         "" , "silent",
         ::CppyyLegacy::option::Arg::None,
         "--silent\tPrint no information at all.\n"
      },

      {
         WRITEEMPTYROOTPCM,
         NOTYPE ,
         "" , "writeEmptyPCM",
         ::CppyyLegacy::option::Arg::None,
         "--writeEmptyPCM\tWrite an empty ROOT pcm.\n"
      },

      {
         CXXMODULE,
         NOTYPE ,
         "" , "cxxmodule",
         ::CppyyLegacy::option::Arg::None,
         "--cxxmodule\tGenerates a PCM for C++ Modules.\n"
      },


      {
         HELP,
         NOTYPE,
         "h" , "help",
         ::CppyyLegacy::option::Arg::None,
         "--help\tPrint usage and exit.\n"
      },

      {
         FAILONWARNINGS,
         NOTYPE,
         "", "fail_on_warnings",
         ::CppyyLegacy::option::Arg::None,
         "--fail_on_warnings\tFail on warnings and errors.\n"
      },

      {
         SELSYNTAXONLY,
         NOTYPE,
         "", "selSyntaxOnly",
         ::CppyyLegacy::option::Arg::None,
         "--selSyntaxOnly\tValidate selection file w/o generating the dictionary.\n"
      },

      {
         NOINCLUDEPATHS,
         NOTYPE ,
         "" , "noIncludePaths",
         ::CppyyLegacy::option::Arg::None,
         "--noIncludePaths\tDo not store the headers' directories in the dictionary. Instead, rely on the environment variable $ROOT_INCLUDE_PATH at runtime.\n"
      },

      // Left intentionally empty not to be shown in the help, like in the first genreflex
      {
         INCLUDE,
         STRING ,
         "I" , "" ,
         ::CppyyLegacy::option::FullArg::Required,
         ""
      },

      {
         PREPROCDEFINE,
         STRING ,
         "D" , "" ,
         ::CppyyLegacy::option::FullArg::Required,
         ""
      },

      {
         PREPROCUNDEFINE,
         STRING ,
         "U" , "" ,
         ::CppyyLegacy::option::FullArg::Required,
         ""
      },

      {
         WARNING,
         STRING ,
         "W" , "" ,
         ::CppyyLegacy::option::FullArg::Required,
         ""
      },

      {
         NOMEMBERTYPEDEFS, // Option which is not meant for the user: deprecated
         STRING ,
         "" , "no_membertypedefs" ,
         ::CppyyLegacy::option::FullArg::None,
         ""
      },

      {
         NOTEMPLATETYPEDEFS, // Option which is not meant for the user: deprecated
         STRING ,
         "" , "no_templatetypedefs" ,
         ::CppyyLegacy::option::FullArg::None,
         ""
      },

      {0, 0, 0, 0, 0, 0}
   };

   std::vector<std::string> headersNames;
   const int originalArgc = argc;
   // The only args are the headers here
   const int extractedArgs = extractArgs(argc, argv, headersNames);

   const int offset = 1; // skip argv[0]
   argc -= offset + extractedArgs;
   argv += offset;

   // Parse the options
   ::CppyyLegacy::option::Stats  stats(genreflexUsageDescriptor,  argc, argv);
   std::vector<::CppyyLegacy::option::Option> options(stats.options_max);// non POD var size arrays are not C++!
   std::vector<::CppyyLegacy::option::Option> buffer(stats.buffer_max);
   // The 4 is the minimum size of the abbreviation length.
   // For example, --selection_file can be abbreviated with --sele at least.

   ::CppyyLegacy::option::Parser parse(genreflexUsageDescriptor, argc, argv, &options[0], &buffer[0], 5);

   if (parse.error()) {
      ::CppyyLegacy::TMetaUtils::Error(0, "Argument parsing error!\n");
      return 1;
   }

   // Print help if needed
   if (options[HELP] || originalArgc == 1) {
      ::CppyyLegacy::option::printUsage(std::cout, genreflexUsageDescriptor);
      return 0;
   }
   // See if no header was provided
   int numberOfHeaders = checkHeadersNames(headersNames);
   if (0 == numberOfHeaders) {
      ::CppyyLegacy::TMetaUtils::Error(0, "No valid header was provided!\n");
      return 1;
   }

   TMetaUtils::GetErrorIgnoreLevel() = TMetaUtils::kNote;

   if (options[DEEP])
      ::CppyyLegacy::TMetaUtils::Warning(0, "--deep has no effect. Please remove the deprecated flag!\n");
   // The verbosity: debug wins over quiet
   //std::string verbosityOption("-v4"); // To be uncommented for the testing phase. It should be -v
   std::string verbosityOption("-v2");
   if (options[SILENT]) verbosityOption = "-v0";
   if (options[VERBOSE] || getenv ("VERBOSE")) verbosityOption = "-v3";
   if (options[DEBUG]) verbosityOption = "-v4";

   genreflex::verbose = verbosityOption == "-v4";

   // The selection file
   std::string selectionFileName;
   if (options[SELECTIONFILENAME]) {
      selectionFileName = options[SELECTIONFILENAME].arg;
      if (!TMetaUtils::EndsWith(selectionFileName, ".xml")) {
         ::CppyyLegacy::TMetaUtils::Error(0,
                                 "Invalid selection file extension: filename is %s and extension .xml is expected!\n",
                                 selectionFileName.c_str());
         return 1;
      }
   }

   // Set the parameters for the rootmap file. If the libname is not set,
   // it will be set according to the header in invokeRootCling.
   // FIXME: treatment of directories
   std::string rootmapFileName(options[ROOTMAP].arg ? options[ROOTMAP].arg : "");
   std::string rootmapLibName(options[ROOTMAPLIB].arg ? options[ROOTMAPLIB].arg : "");

   // The target lib name
   std::string targetLibName;
   if (options[TARGETLIB]) {
      targetLibName = options[TARGETLIB].arg;
      if (!IsGoodLibraryName(targetLibName)) {
         ::CppyyLegacy::TMetaUtils::Error("",
                                 "Invalid target library extension: filename is %s and extension %s is expected!\n",
                                 targetLibName.c_str(),
                                 gLibraryExtension.c_str());
      }
      // Target lib has precedence over rootmap lib
      if (options[ROOTMAP]) {
         rootmapLibName = ExtractFileName(options[TARGETLIB].arg);
      }
   }

   bool isCxxmodule = options[CXXMODULE];

   // Optional extra flags
   std::string extraFlags;
   if (options[EXTRAFLAGS])
       extraFlags = options[EXTRAFLAGS].arg;

   bool multidict = false;
   if (options[MULTIDICT]) multidict = true;

   if (multidict && targetLibName.empty()) {
      ::CppyyLegacy::TMetaUtils::Error("",
                              "Multilib support is requested but no target lib is specified. A sane pcm name cannot be formed.\n");
      return 1;
   }

   bool interpreteronly = false;
   if (options[INTERPRETERONLY])
      interpreteronly = true;

   bool doSplit = false;
   if (options[SPLIT])
      doSplit = true;

   bool writeEmptyRootPCM = false;
   if (options[WRITEEMPTYROOTPCM])
      writeEmptyRootPCM = true;

   bool selSyntaxOnly = false;
   if (options[SELSYNTAXONLY]) {
      selSyntaxOnly = true;
   }

   bool noIncludePaths = false;
   if (options[NOINCLUDEPATHS]) {
      noIncludePaths = true;
   }

   bool failOnWarnings = false;
   if (options[FAILONWARNINGS]) {
      failOnWarnings = true;
   }

   // Add the .so extension to the rootmap lib if not there
   if (!rootmapLibName.empty() && !IsGoodLibraryName(rootmapLibName)) {
      rootmapLibName += gLibraryExtension;
   }

   // The list of pcms to be preloaded
   std::vector<std::string> pcmsNames;
   extractMultipleOptions(options, PCMFILENAME, pcmsNames);

   // Preprocessor defines
   std::vector<std::string> preprocDefines;
   extractMultipleOptions(options, PREPROCDEFINE, preprocDefines);

   // Preprocessor undefines
   std::vector<std::string> preprocUndefines;
   extractMultipleOptions(options, PREPROCUNDEFINE, preprocUndefines);

   // Includes
   std::vector<std::string> includes;
   extractMultipleOptions(options, INCLUDE, includes);

   // Warnings
   std::vector<std::string> warnings;
   extractMultipleOptions(options, WARNING, warnings);

   // The outputfilename(s)
   // There are two cases:
   // 1) The outputfilename is specified
   //   -->  The information of all headers will be in one single dictionary
   //     (1 call to rootcling)
   // 2) The outputfilename is not specified
   //   --> There will be a dictionary per header
   //     (N calls to rootcling)
   int returnValue = 0;
   std::string ofileName(options[OFILENAME] ? options[OFILENAME].arg : "");

   // If not empty and not a directory (therefore it's a file)
   // call rootcling directly. The number of headers files is irrelevant.
   if (!ofileName.empty() && !llvm::sys::fs::is_directory(ofileName)) {
      returnValue = invokeRootCling(verbosityOption,
                                    selectionFileName,
                                    targetLibName,
                                    multidict,
                                    pcmsNames,
                                    includes,
                                    preprocDefines,
                                    preprocUndefines,
                                    warnings,
                                    rootmapFileName,
                                    rootmapLibName,
                                    extraFlags,
                                    interpreteronly,
                                    doSplit,
                                    isCxxmodule,
                                    writeEmptyRootPCM,
                                    selSyntaxOnly,
                                    noIncludePaths,
                                    headersNames,
                                    failOnWarnings,
                                    ofileName);
   } else {
      // Here ofilename is either "" or a directory: this is irrelevant.
      returnValue = invokeManyRootCling(verbosityOption,
                                        selectionFileName,
                                        targetLibName,
                                        multidict,
                                        pcmsNames,
                                        includes,
                                        preprocDefines,
                                        preprocUndefines,
                                        warnings,
                                        rootmapFileName,
                                        rootmapLibName,
                                        extraFlags,
                                        interpreteronly,
                                        doSplit,
                                        isCxxmodule,
                                        writeEmptyRootPCM,
                                        selSyntaxOnly,
                                        noIncludePaths,
                                        headersNames,
                                        failOnWarnings,
                                        ofileName);
   }

   return returnValue;
}


////////////////////////////////////////////////////////////////////////////////

extern "C"
int ROOT_rootcling_Driver(int argc, char **argv, const CppyyLegacy::Internal::RootCling::DriverConfig& config)
{

   assert(!gDriverConfig && "Driver configuration already set!");
   gDriverConfig = &config;

   gBuildingROOT = config.fBuildingROOTStage1; // gets refined later

   std::string exeName = ExtractFileName(GetExePath());

   // Select according to the name of the executable the procedure to follow:
   // 1) RootCling
   // 2) GenReflex
   // The default is rootcling

   int retVal = 0;

   if (std::string::npos != exeName.find("genreflex"))
      retVal = GenReflexMain(argc, argv);
   else // rootcling or default
      retVal = RootClingMain(argc, argv);

   gDriverConfig = nullptr;

   if (CppyyLegacy::TMetaUtils::GetNumberOfErrors()){
      CppyyLegacy::TMetaUtils::Info(0,"Problems have been detected during the generation of the dictionary.\n");
      return 1;
   }
   return retVal;
}

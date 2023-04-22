#ifndef WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
// silence warnings about getenv, strncpy, etc.
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

// Bindings
#include "capi.h"
#include "cpp_cppyy.h"
#include "callcontext.h"

#include <dlfcn.h>

// Standard
#include <assert.h>
#include <algorithm>     // for std::count, std::remove
#include <stdexcept>
#include <map>
#include <new>
#include <regex>
#include <set>
#include <sstream>
#include <signal.h>
#include <stdlib.h>      // for getenv
#include <string.h>
#include <typeinfo>
#include <iostream>


// temp
#include <iostream>
// --temp

// data for life time management ---------------------------------------------
// typedef std::vector<TClassRef> ClassRefs_t;
// static ClassRefs_t g_classrefs(1);
// static const ClassRefs_t::size_type GLOBAL_HANDLE = 1;
// static const ClassRefs_t::size_type STD_HANDLE = GLOBAL_HANDLE + 1;

// typedef std::map<std::string, ClassRefs_t::size_type> Name2ClassRefIndex_t;
// static Name2ClassRefIndex_t g_name2classrefidx;

// namespace {

// static inline
// Cppyy::TCppType_t find_memoized(const std::string& name)
// {
//     auto icr = g_name2classrefidx.find(name);
//     if (icr != g_name2classrefidx.end())
//         return (Cppyy::TCppType_t)icr->second;
//     return (Cppyy::TCppType_t)0;
// }
//
// } // namespace
//
// static inline
// CallWrapper* new_CallWrapper(CppyyLegacy::TFunction* f)
// {
//     CallWrapper* wrap = new CallWrapper(f);
//     gWrapperHolder.push_back(wrap);
//     return wrap;
// }
//

// typedef std::vector<TGlobal*> GlobalVars_t;
// typedef std::map<TGlobal*, GlobalVars_t::size_type> GlobalVarsIndices_t;

// static GlobalVars_t g_globalvars;
// static GlobalVarsIndices_t g_globalidx;


// builtin types
static std::set<std::string> g_builtins =
    {"bool", "char", "signed char", "unsigned char", "wchar_t", "short", "unsigned short",
     "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long",
     "float", "double", "long double", "void"};

// smart pointer types
static std::set<std::string> gSmartPtrTypes =
    {"std::auto_ptr", "std::shared_ptr", "std::unique_ptr", "std::weak_ptr"};

// to filter out ROOT names
static std::set<std::string> gInitialNames;
static std::set<std::string> gRootSOs;

// configuration
static bool gEnableFastPath = true;


// global initialization -----------------------------------------------------
namespace {

const int kMAXSIGNALS = 16;

// names copied from TUnixSystem
#ifdef WIN32
const int SIGBUS   = 0;      // simple placeholders for ones that don't exist
const int SIGSYS   = 0;
const int SIGPIPE  = 0;
const int SIGQUIT  = 0;
const int SIGWINCH = 0;
const int SIGALRM  = 0;
const int SIGCHLD  = 0;
const int SIGURG   = 0;
const int SIGUSR1  = 0;
const int SIGUSR2  = 0;
#endif

static struct Signalmap_t {
   int               fCode;
   const char       *fSigName;
} gSignalMap[kMAXSIGNALS] = {       // the order of the signals should be identical
   { SIGBUS,   "bus error" }, // to the one in TSysEvtHandler.h
   { SIGSEGV,  "segmentation violation" },
   { SIGSYS,    "bad argument to system call" },
   { SIGPIPE,   "write on a pipe with no one to read it" },
   { SIGILL,    "illegal instruction" },
   { SIGABRT,   "abort" },
   { SIGQUIT,   "quit" },
   { SIGINT,    "interrupt" },
   { SIGWINCH,  "window size change" },
   { SIGALRM,   "alarm clock" },
   { SIGCHLD,   "death of a child" },
   { SIGURG,    "urgent data arrived on an I/O channel" },
   { SIGFPE,    "floating point exception" },
   { SIGTERM,   "termination signal" },
   { SIGUSR1,   "user-defined signal 1" },
   { SIGUSR2,   "user-defined signal 2" }
};

// static void inline do_trace(int sig) {
//     std::cerr << " *** Break *** " << (sig < kMAXSIGNALS ? gSignalMap[sig].fSigName : "") << std::endl;
//     gSystem->StackTrace();
// }

// class TExceptionHandlerImp : public TExceptionHandler {
// public:
//     virtual void HandleException(Int_t sig) {
//         if (TROOT::Initialized()) {
//             if (gException) {
//                 gInterpreter->RewindDictionary();
//                 gInterpreter->ClearFileBusy();
//             }
//
//             if (!getenv("CPPYY_CRASH_QUIET"))
//                 do_trace(sig);
//
//         // jump back, if catch point set
//             Throw(sig);
//         }
//
//         do_trace(sig);
//         gSystem->Exit(128 + sig);
//     }
// };

static InterOp::TInterp_t getInterp() {
    static InterOp::TInterp_t gInterp = InterOp::CreateInterpreter({"-std=c++17",
          "-march=native"});
    return gInterp;
}

static InterOp::TCppSema_t getSema() {
    static InterOp::TInterp_t gSema = InterOp::GetSema(getInterp());
    return gSema;
}

class ApplicationStarter {
public:
    ApplicationStarter() {
        // Create the interpreter and initilize the pointer
        InterOp::TInterp_t I = getInterp();
        InterOp::TCppSema_t S  = getSema();

        // fill out the builtins
        std::set<std::string> bi{g_builtins};
        for (const auto& name : bi) {
            for (const char* a : {"*", "&", "*&", "[]", "*[]"})
                g_builtins.insert(name+a);
        }

    // disable fast path if requested
        if (getenv("CPPYY_DISABLE_FASTPATH")) gEnableFastPath = false;

    // set opt level (default to 2 if not given; Cling itself defaults to 0)
        int optLevel = 2;

        if (getenv("CPPYY_OPT_LEVEL")) optLevel = atoi(getenv("CPPYY_OPT_LEVEL"));

        if (optLevel != 0) {
            std::ostringstream s;
            s << "#pragma cling optimize " << optLevel;
            InterOp::Process(I, s.str().c_str());
        }

        // This would give us something like:
        // /home/vvassilev/workspace/builds/scratch/cling-build/builddir/lib/clang/13.0.0
        const char * ResourceDir = InterOp::GetResourceDir(I);
        std::string ClingSrc = std::string(ResourceDir) + "/../../../../cling-src";
        std::string ClingBuildDir = std::string(ResourceDir) + "/../../../";
        InterOp::AddIncludePath(I, (ClingSrc + "/tools/cling/include").c_str());
        InterOp::AddIncludePath(I, (ClingSrc + "/include").c_str());
        InterOp::AddIncludePath(I, (ClingBuildDir + "/include").c_str());
        InterOp::AddIncludePath(I, (std::string(INTEROP_DIR) + "/include").c_str());
        InterOp::LoadLibrary(I, "libstdc++", /* lookup= */ true);

        // load frequently used headers
        const char* code =
               "#include <iostream>\n"
               "#include <string>\n"
            //    "#include <DllImport.h>\n"     // defines R__EXTERN
               "#include <vector>\n"
               "#include <utility>\n"
               "#include <memory>\n"
               "#include <functional>\n" // for the dispatcher code to use std::function
               "#include <map>\n" // FIXME: Replace with modules
               "#include <sstream>\n" // FIXME: Replace with modules
               "#include <array>\n" // FIXME: Replace with modules
               "#include <list>\n" // FIXME: Replace with modules
               "#include <deque>\n" // FIXME: Replace with modules
               "#include <tuple>\n" // FIXME: Replace with modules
               "#include <set>\n" // FIXME: Replace with modules
               "#include <chrono>\n" // FIXME: Replace with modules
               "#include <cmath>\n" // FIXME: Replace with modules
               "#include \"cling/Interpreter/Interpreter.h\"\n"
               "#include \"clang/Interpreter/InterOp.h\"";
        InterOp::Process(I, code);

    // create helpers for comparing thingies
        InterOp::Declare(I, 
            "namespace __cppyy_internal { template<class C1, class C2>"
            " bool is_equal(const C1& c1, const C2& c2) { return (bool)(c1 == c2); } }");
        InterOp::Declare(I, 
            "namespace __cppyy_internal { template<class C1, class C2>"
            " bool is_not_equal(const C1& c1, const C2& c2) { return (bool)(c1 != c2); } }");
        InterOp::Process(I, 
            "namespace cling { namespace runtime {"
            " DynamicLibraryManager *gDLM = gCling->getDynamicLibraryManager(); } }");

    // helper for multiple inheritance
        InterOp::Declare(I, "namespace __cppyy_internal { struct Sep; }");

        // std::string libInterOp = I->getDynamicLibraryManager()->lookupLibrary("libcling");
        // void *interopDL = dlopen(libInterOp.c_str(), RTLD_LAZY);
        // if (!interopDL) {
        //     std::cerr << "libInterop could not be opened!\n";
        //     exit(1);
        // }

    // start off with a reasonable size placeholder for wrappers
        // gWrapperHolder.reserve(1024);

    // create an exception handler to process signals
        // gExceptionHandler = new TExceptionHandlerImp{};
    }

    ~ApplicationStarter() {
        // for (auto wrap : gWrapperHolder)
        //     delete wrap;
        // delete gExceptionHandler; gExceptionHandler = nullptr;
    }
} _applicationStarter;

} // unnamed namespace


// // local helpers -------------------------------------------------------------
// static inline
// TClassRef& type_from_handle(Cppyy::TCppScope_t scope)
// {
//     assert((ClassRefs_t::size_type)scope < g_classrefs.size());
//     return g_classrefs[(ClassRefs_t::size_type)scope];
// }
//
// static inline
// TFunction* m2f(Cppyy::TCppMethod_t method) {
//     CallWrapper *wrap = (CallWrapper *)method;
//
//     if (!wrap->fTF) {
//         MethodInfo_t* mi = gInterpreter->MethodInfo_Factory(wrap->fDecl);
//         wrap->fTF = new TFunction(mi);
//     }
//     return (TFunction *) wrap->fTF;
// }
//
static inline
char* cppstring_to_cstring(const std::string& cppstr)
{
    char* cstr = (char*)malloc(cppstr.size()+1);
    memcpy(cstr, cppstr.c_str(), cppstr.size()+1);
    return cstr;
}
//
// static inline
// bool match_name(const std::string& tname, const std::string fname)
// {
// // either match exactly, or match the name as template
//     if (fname.rfind(tname, 0) == 0) {
//         if ((tname.size() == fname.size()) ||
//               (tname.size() < fname.size() && fname[tname.size()] == '<'))
//            return true;
//     }
//     return false;
// }
//
//
// // direct interpreter access -------------------------------------------------
// Returns false on failure and true on success
bool Cppyy::Compile(const std::string& code, bool silent)
{
    // Declare returns an enum which equals 0 on success
    return !InterOp::Declare(getInterp(), code.c_str(), silent);
}

std::string Cppyy::ToString(TCppType_t klass, TCppObject_t obj)
{
    if (klass && obj && !InterOp::IsNamespace((TCppScope_t)klass))
        return InterOp::ObjToString(
                getInterp(), 
                InterOp::GetCompleteName(getSema(), klass).c_str(),
                (void*)obj);
    return "";
}

// // name to opaque C++ scope representation -----------------------------------
std::string Cppyy::ResolveName(const std::string& cppitem_name)
{
#ifdef PRINT_DEBUG
    printf("Resolve name input = %s\n", cppitem_name.c_str());
#endif
    return cppitem_name;

// // Fully resolve the given name to the final type name.
//
// // try memoized type cache, in case seen before
//     TCppType_t klass = find_memoized(cppitem_name);
//     if (klass) return GetScopedFinalName(klass);
//
// // remove global scope '::' if present
//     std::string tclean = cppitem_name.compare(0, 2, "::") == 0 ?
//         cppitem_name.substr(2, std::string::npos) : cppitem_name;
//
// // classes (most common)
//     tclean = TClassEdit::CleanType(tclean.c_str());
//     if (tclean.empty() [> unknown, eg. an operator <]) return cppitem_name;
//
// // reduce [N] to []
//     if (tclean[tclean.size()-1] == ']')
//         tclean = tclean.substr(0, tclean.rfind('[')) + "[]";
//
// // remove __restrict and __restrict__
//     auto pos = tclean.rfind("__restrict");
//     if (pos != std::string::npos)
//         tclean = tclean.substr(0, pos);
//
//     if (tclean.compare(0, 9, "std::byte") == 0)
//         return tclean;
//
// // check data types list (accept only builtins as typedefs will
// // otherwise not be resolved)
//     if (IsBuiltin(tclean)) return tclean;
//
// // special case for enums
//     if (IsEnum(cppitem_name))
//         return ResolveEnum(cppitem_name);
//
// // special case for clang's builtin __type_pack_element (which does not resolve)
//     pos = cppitem_name.size() > 20 ? \
//               cppitem_name.rfind("__type_pack_element", 5) : std::string::npos;
//     if (pos != std::string::npos) {
//     // shape is "[std::]__type_pack_element<index,type1,type2,...,typeN>cpd": extract
//     // first the index, and from there the indexed type; finally, restore the
//     // qualifiers
//         const char* str = cppitem_name.c_str();
//         char* endptr = nullptr;
//         unsigned long index = strtoul(str+20+pos, &endptr, 0);
//
//         std::string tmplvars{endptr};
//         auto start = tmplvars.find(',') + 1;
//         auto end = tmplvars.find(',', start);
//         while (index != 0) {
//             start = end+1;
//             end = tmplvars.find(',', start);
//             if (end == std::string::npos) end = tmplvars.rfind('>');
//             --index;
//         }
//
//         std::string resolved = tmplvars.substr(start, end-start);
//         auto cpd = tmplvars.rfind('>');
//         if (cpd != std::string::npos && cpd+1 != tmplvars.size())
//             return resolved + tmplvars.substr(cpd+1, std::string::npos);
//         return resolved;
//     }
//
// // typedefs etc. (and a couple of hacks around TClassEdit-isms, fixing of which
// // in ResolveTypedef itself is a TODO ...)
//     tclean = TClassEdit::ResolveTypedef(tclean.c_str(), true);
//     pos = 0;
//     while ((pos = tclean.find("::::", pos)) != std::string::npos) {
//         tclean.replace(pos, 4, "::");
//         pos += 2;
//     }
//
//     if (tclean.compare(0, 6, "const ") != 0)
//         return TClassEdit::ShortType(tclean.c_str(), 2);
//     return "const " + TClassEdit::ShortType(tclean.c_str(), 2);
}


Cppyy::TCppType_t Cppyy::ResolveType(TCppType_t type) {
    Cppyy::TCppType_t canonType = InterOp::GetCanonicalType(type);

    if (InterOp::IsEnumType(canonType)) {
        return InterOp::GetIntegerTypeFromEnumType(canonType);
    }

    return canonType;
}

Cppyy::TCppType_t Cppyy::GetRealType(TCppType_t type) {
    return InterOp::GetUnderlyingType(type);
}

bool Cppyy::IsClassType(TCppType_t type) {
    return InterOp::IsRecordType(type);
}

Cppyy::TCppType_t Cppyy::GetType(const std::string &name, bool enable_slow_lookup /* = false */) {
    static unsigned long long var_count = 0;

    if (auto type = InterOp::GetType(getSema(), name))
        return type;

    if (!enable_slow_lookup) {
        if (name.find("::") != std::string::npos)
            throw std::runtime_error("Calling Cppyy::GetType with qualified name '"
                                + name + "'\n");
        return nullptr;
    }

    std::string id = "__Cppyy_GetType_" + std::to_string(var_count++);

    InterOp::Declare(getInterp(), ("using " + id + " = __typeof__(" + name + ");\n").c_str());

    TCppScope_t lookup = InterOp::GetNamed(getSema(), id, 0);

    return InterOp::GetUnderlyingType(InterOp::GetTypeFromScope(lookup));
}


Cppyy::TCppType_t Cppyy::GetComplexType(const std::string &name) {
    return InterOp::GetComplexType(getSema(), InterOp::GetType(getSema(), name));
}


// //----------------------------------------------------------------------------
// static std::string extract_namespace(const std::string& name)
// {
// // Find the namespace the named class lives in, take care of templates
// // Note: this code also lives in CPyCppyy (TODO: refactor?)
//     if (name.empty())
//         return name;
//
//     int tpl_open = 0;
//     for (std::string::size_type pos = name.size()-1; 0 < pos; --pos) {
//         std::string::value_type c = name[pos];
//
//     // count '<' and '>' to be able to skip template contents
//         if (c == '>')
//             ++tpl_open;
//         else if (c == '<')
//             --tpl_open;
//
//     // collect name up to "::"
//         else if (tpl_open == 0 && c == ':' && name[pos-1] == ':') {
//         // found the extend of the scope ... done
//             return name.substr(0, pos-1);
//         }
//     }
//
// // no namespace; assume outer scope
//     return "";
// }
//

std::string Cppyy::ResolveEnum(TCppScope_t handle)
{
    return InterOp::GetTypeAsString(
        InterOp::GetIntegerTypeFromEnumScope(handle));
}

Cppyy::TCppScope_t Cppyy::GetScope(const std::string& name,
                                   TCppScope_t parent_scope)
{
    if (name.find("::") != std::string::npos) {
#ifdef PRINT_DEBUG
        printf("Wrong call to GetScope\n");
#endif
    }

    return InterOp::GetScope(getSema(), name, parent_scope);
}

Cppyy::TCppScope_t Cppyy::GetFullScope(const std::string& name)
{
    return InterOp::GetScopeFromCompleteName(getSema(), name);
}

Cppyy::TCppScope_t Cppyy::GetTypeScope(TCppScope_t var)
{
    return InterOp::GetScopeFromType(
        InterOp::GetVariableType(var));
}

Cppyy::TCppScope_t Cppyy::GetNamed(const std::string& name,
                                   TCppScope_t parent_scope)
{
    return InterOp::GetNamed(getSema(), name, parent_scope);
}

Cppyy::TCppScope_t Cppyy::GetParentScope(TCppScope_t scope)
{
    return InterOp::GetParentScope(scope);
}

Cppyy::TCppScope_t Cppyy::GetScopeFromType(TCppType_t type)
{
    return InterOp::GetScopeFromType(type);
}

Cppyy::TCppType_t Cppyy::GetTypeFromScope(TCppScope_t klass)
{
    return InterOp::GetTypeFromScope(klass);
}

Cppyy::TCppScope_t Cppyy::GetGlobalScope()
{
    return InterOp::GetGlobalScope(getSema());
}

bool Cppyy::IsTemplate(TCppScope_t handle)
{
    return InterOp::IsTemplate(handle);
}

bool Cppyy::IsTemplateInstantiation(TCppScope_t handle)
{
    return InterOp::IsTemplateSpecialization(handle);
}

// namespace {
//     class AutoCastRTTI {
//     public:
//         virtual ~AutoCastRTTI() {}
//     };
// }
//
// Cppyy::TCppType_t Cppyy::GetActualClass(TCppType_t klass, TCppObject_t obj)
// {
//     TClassRef& cr = type_from_handle(klass);
//     if (!cr.GetClass() || !obj) return klass;
//
//     if (!(cr->ClassProperty() & kClassHasVirtual))
//         return klass;   // not polymorphic: no RTTI info available
//
// #ifdef _WIN64
// // Cling does not provide a consistent ImageBase address for calculating relative addresses
// // as used in Windows 64b RTTI. So, check for our own RTTI extension instead. If that fails,
// // see whether the unmangled raw_name is available (e.g. if this is an MSVC compiled rather
// // than JITed class) and pass on if it is.
//     volatile const char* raw = nullptr;     // to prevent too aggressive reordering
//     try {
//     // this will filter those objects that do not have RTTI to begin with (throws)
//         AutoCastRTTI* pcst = (AutoCastRTTI*)obj;
//         raw = typeid(*pcst).raw_name();
//
//     // check the signature id (0 == absolute, 1 == relative, 2 == ours)
//         void* vfptr = *(void**)((intptr_t)obj);
//         void* meta = (void*)((intptr_t)*((void**)((intptr_t)vfptr-sizeof(void*))));
//         if (*(intptr_t*)meta == 2) {
//         // access the extra data item which is an absolute pointer to the RTTI
//             void* ptdescr = (void*)((intptr_t)meta + 4*sizeof(unsigned long)+sizeof(void*));
//             if (ptdescr && *(void**)ptdescr) {
//                 auto rtti = *(std::type_info**)ptdescr;
//                 raw = rtti->raw_name();
//                 if (raw && raw[0] != '\0')    // likely unnecessary
//                     return (TCppType_t)GetScope(rtti->name());
//             }
//
//             return klass;        // do not fall through if no RTTI info available
//         }
//
//     // if the raw name is the empty string (no guarantees that this is so as truly, the
//     // address is corrupt, but it is common to be empty), then there is no accessible RTTI
//     // and getting the unmangled name will crash ...
//
//     // a common case are the i/o stream objects b/c these are pulled in from libCling
//         if (!raw || (strstr(cr->GetName(), "std::basic_") && strstr(cr->GetName(), "stream")) || raw[0] == '\0')
//             return klass;
//     } catch (std::bad_typeid) {
//         return klass;        // can't risk passing to ROOT/meta as it may do RTTI
//     }
// #endif
//
//     TClass* clActual = cr->GetActualClass((void*)obj);
//     if (clActual && clActual != cr.GetClass()) {
//         auto itt = g_name2classrefidx.find(clActual->GetName());
//         if (itt != g_name2classrefidx.end())
//             return (TCppType_t)itt->second;
//         return (TCppType_t)GetScope(clActual->GetName());
//     }
//
//     return klass;
// }

size_t Cppyy::SizeOf(TCppType_t klass)
{
    return InterOp::SizeOf(klass);
}

// size_t Cppyy::SizeOf(const std::string& type_name)
// {
//     TDataType* dt = gROOT->GetType(type_name.c_str());
//     if (dt) return dt->Size();
//     return SizeOf(GetScope(type_name));
// }

bool Cppyy::IsBuiltin(const std::string& type_name)
{
    static std::set<std::string> s_builtins =
       {"bool", "char", "signed char", "unsigned char", "wchar_t", "short",
        "unsigned short", "int", "unsigned int", "long", "unsigned long",
        "long long", "unsigned long long", "float", "double", "long double",
        "void"};
     if (s_builtins.find(type_name) != s_builtins.end())
         return true;

    if (strstr(type_name.c_str(), "std::complex"))
        return true;

    return false;
}

bool Cppyy::IsBuiltin(TCppType_t type)
{
    return  InterOp::IsBuiltin(type);
    
}

bool Cppyy::IsComplete(TCppScope_t scope)
{
    return InterOp::IsComplete(scope);
}

// // memory management ---------------------------------------------------------
// Cppyy::TCppObject_t Cppyy::Allocate(TCppType_t type)
// {
//     TClassRef& cr = type_from_handle(type);
//     return (TCppObject_t)::operator new(gInterpreter->ClassInfo_Size(cr->GetClassInfo()));
// }
//
// void Cppyy::Deallocate(TCppType_t /* type */, TCppObject_t instance)
// {
//     ::operator delete(instance);
// }
//
// Cppyy::TCppObject_t Cppyy::Construct(TCppType_t type)
// {
//     TClassRef& cr = type_from_handle(type);
//     return (TCppObject_t)cr->New();
// }
//
// static std::map<Cppyy::TCppType_t, bool> sHasOperatorDelete;
// void Cppyy::Destruct(TCppType_t type, TCppObject_t instance)
// {
//     TClassRef& cr = type_from_handle(type);
//     if (cr->ClassProperty() & (kClassHasExplicitDtor | kClassHasImplicitDtor))
//         cr->Destructor((void*)instance);
//     else {
//         ::CppyyLegacy::DelFunc_t fdel = cr->GetDelete();
//         if (fdel) fdel((void*)instance);
//         else {
//             auto ib = sHasOperatorDelete.find(type);
//             if (ib == sHasOperatorDelete.end()) {
//                 TFunction* f = (TFunction*)cr->GetMethodAllAny("operator delete");
//                 sHasOperatorDelete[type] = (bool)(f && (f->Property() & kIsPublic));
//                 ib = sHasOperatorDelete.find(type);
//             }
//             ib->second ? cr->Destructor((void*)instance) : free((void*)instance);
//         }
//     }
// }

static inline
bool copy_args(Parameter* args, size_t nargs, void** vargs)
{
    bool runRelease = false;
    for (size_t i = 0; i < nargs; ++i) {
        switch (args[i].fTypeCode) {
        case 'X':       /* (void*)type& with free */
            runRelease = true;
        case 'V':       /* (void*)type& */
            vargs[i] = args[i].fValue.fVoidp;
            break;
        case 'r':       /* const type& */
            vargs[i] = args[i].fRef;
            break;
        default:        /* all other types in union */
            vargs[i] = (void*)&args[i].fValue.fVoidp;
            break;
        }
    }
    return runRelease;
}

static inline
void release_args(Parameter* args, size_t nargs) {
    for (size_t i = 0; i < nargs; ++i) {
        if (args[i].fTypeCode == 'X')
            free(args[i].fValue.fVoidp);
    }
}

// static inline
// bool is_ready(CallWrapper* wrap, bool is_direct) {
//     return (!is_direct && wrap->fFaceptr.fGeneric) || (is_direct && wrap->fFaceptr.fDirect);
// }

static inline
bool WrapperCall(Cppyy::TCppMethod_t method, size_t nargs, void* args_, void* self, void* result)
{
    Parameter* args = (Parameter*)args_;
    bool is_direct = nargs & DIRECT_CALL;
    nargs = CALL_NARGS(nargs);

    // CallWrapper* wrap = (CallWrapper*)method;
    const InterOp::CallFuncWrapper_t& faceptr = 
        InterOp::GetFunctionCallWrapper(getInterp(), method);
    // if (!is_ready(wrap, is_direct))
    //     return false;        // happens with compilation error

    nargs = CALL_NARGS(nargs);
    // if (faceptr.fKind == InterOp::CallFuncWrapper_t::kGeneric) {
    bool runRelease = false;
    const auto& fgen = /* is_direct ? faceptr.fDirect : */ faceptr;
    if (nargs <= SMALL_ARGS_N) {
        void* smallbuf[SMALL_ARGS_N];
        if (nargs) runRelease = copy_args(args, nargs, smallbuf);
        // CLING_CATCH_UNCAUGHT_
#ifdef PRINT_DEBUG
        printf("start execution\n");
#endif
        fgen(self, (int)nargs, smallbuf, result);
#ifdef PRINT_DEBUG
        printf("executed\n");
#endif
        // _CLING_CATCH_UNCAUGHT
    } else {
        std::vector<void*> buf(nargs);
        runRelease = copy_args(args, nargs, buf.data());
        // CLING_CATCH_UNCAUGHT_
        fgen(self, (int)nargs, buf.data(), result);
        // _CLING_CATCH_UNCAUGHT
    }
    if (runRelease) release_args(args, nargs);
    return true;
    // }

    // if (faceptr.fKind == InterOp::CallFuncWrapper_t::kCtor) {
    //     bool runRelease = false;
    //     if (nargs <= SMALL_ARGS_N) {
    //         void* smallbuf[SMALL_ARGS_N];
    //         if (nargs) runRelease = copy_args(args, nargs, (void**)smallbuf);
    //         // CLING_CATCH_UNCAUGHT_
    //         faceptr.fCtor((void**)smallbuf, result, (unsigned long)nargs);
    //         // _CLING_CATCH_UNCAUGHT
    //     } else {
    //         std::vector<void*> buf(nargs);
    //         runRelease = copy_args(args, nargs, buf.data());
    //         // CLING_CATCH_UNCAUGHT_
    //         faceptr.fCtor(buf.data(), result, (unsigned long)nargs);
    //         // _CLING_CATCH_UNCAUGHT
    //     }
    //     if (runRelease) release_args(args, nargs);
    //     return true;
    // }

    // if (faceptr.fKind == InterOp::CallFuncWrapper_t::kDtor) {
    //     std::cerr << " DESTRUCTOR NOT IMPLEMENTED YET! " << std::endl;
    //     return false;
    // }

    return false;
}

template<typename T>
static inline
T CallT(Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, size_t nargs, void* args)
{
    T t{};
    if (WrapperCall(method, nargs, args, (void*)self, &t))
        return t;
    throw std::runtime_error("failed to resolve function");
    return (T)-1;
}

#ifdef PRINT_DEBUG
    #define _IMP_CALL_PRINT_STMT(type)                                       \
        printf("IMP CALL with type: %s\n", #type);
#else
    #define _IMP_CALL_PRINT_STMT(type)
#endif

#define CPPYY_IMP_CALL(typecode, rtype)                                      \
rtype Cppyy::Call##typecode(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args)\
{                                                                            \
    _IMP_CALL_PRINT_STMT(rtype)                                              \
    return CallT<rtype>(method, self, nargs, args);                          \
}

void Cppyy::CallV(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args)
{
    if (!WrapperCall(method, nargs, args, (void*)self, nullptr))
        return /* TODO ... report error */;
}

CPPYY_IMP_CALL(B,  unsigned char)
CPPYY_IMP_CALL(C,  char         )
CPPYY_IMP_CALL(H,  short        )
CPPYY_IMP_CALL(I,  int          )
CPPYY_IMP_CALL(L,  long         )
CPPYY_IMP_CALL(LL, long long    )
CPPYY_IMP_CALL(F,  float        )
CPPYY_IMP_CALL(D,  double       )
CPPYY_IMP_CALL(LD, long double  )

void* Cppyy::CallR(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args)
{
    void* r = nullptr;
    if (WrapperCall(method, nargs, args, (void*)self, &r))
        return r;
    return nullptr;
}

char* Cppyy::CallS(
    TCppMethod_t method, TCppObject_t self, size_t nargs, void* args, size_t* length)
{
    char* cstr = nullptr;
    // TClassRef cr("std::string"); // TODO: Why is this required?
    std::string* cppresult = (std::string*)malloc(sizeof(std::string));
    if (WrapperCall(method, nargs, args, self, (void*)cppresult)) {
        cstr = cppstring_to_cstring(*cppresult);
        *length = cppresult->size();
        cppresult->std::string::~basic_string();
    } else
        *length = 0;
    free((void*)cppresult);
    return cstr;
}

Cppyy::TCppObject_t Cppyy::CallConstructor(
    TCppMethod_t method, TCppScope_t /* klass */, size_t nargs, void* args)
{
    void* obj = nullptr;
    if (WrapperCall(method, nargs, args, nullptr, &obj))
        return (TCppObject_t)obj;
    return (TCppObject_t)0;
}

// void Cppyy::CallDestructor(TCppType_t type, TCppObject_t self)
// {
//     TClassRef& cr = type_from_handle(type);
//     cr->Destructor((void*)self, true);
// }

Cppyy::TCppObject_t Cppyy::CallO(TCppMethod_t method,
    TCppObject_t self, size_t nargs, void* args, TCppType_t result_type)
{
    void* obj = ::operator new(InterOp::GetSizeOfType(getSema(), result_type));
    if (WrapperCall(method, nargs, args, self, obj))
        return (TCppObject_t)obj;
    ::operator delete(obj);
    return (TCppObject_t)0;
}

Cppyy::TCppFuncAddr_t Cppyy::GetFunctionAddress(TCppMethod_t method, bool check_enabled)
{
    return (TCppFuncAddr_t) InterOp::GetFunctionAddress(getInterp(), method);
}


// handling of function argument buffer --------------------------------------
void* Cppyy::AllocateFunctionArgs(size_t nargs)
{
    return new Parameter[nargs];
}

void Cppyy::DeallocateFunctionArgs(void* args)
{
    delete [] (Parameter*)args;
}

size_t Cppyy::GetFunctionArgSizeof()
{
    return sizeof(Parameter);
}

size_t Cppyy::GetFunctionArgTypeoffset()
{
    return offsetof(Parameter, fTypeCode);
}


// scope reflection information ----------------------------------------------
bool Cppyy::IsNamespace(TCppScope_t scope)
{
    if (!scope)
      return false;

    // Test if this scope represents a namespace.
    return InterOp::IsNamespace(scope) || 
        InterOp::GetGlobalScope(getSema()) == scope;
}

bool Cppyy::IsClass(TCppScope_t scope)
{
    // Test if this scope represents a namespace.
    return InterOp::IsClass(scope);
}
//
bool Cppyy::IsAbstract(TCppScope_t scope)
{
    // Test if this type may not be instantiated.
    return InterOp::IsAbstract(scope);
}

bool Cppyy::IsEnumScope(TCppScope_t scope)
{
    return InterOp::IsEnumScope(scope);
}

bool Cppyy::IsEnumConstant(TCppScope_t scope)
{
    return InterOp::IsEnumConstant(scope);
}

bool Cppyy::IsEnumType(TCppType_t type)
{
    return InterOp::IsEnumType(type);
}

bool Cppyy::IsAggregate(TCppType_t type)
{
  // Test if this type is a "plain old data" type
  return InterOp::IsAggregate(type);
}

bool Cppyy::IsDefaultConstructable(TCppScope_t scope)
{
// Test if this type has a default constructor or is a "plain old data" type
    return InterOp::HasDefaultConstructor(scope);
}

bool Cppyy::IsVariable(TCppScope_t scope)
{
    return InterOp::IsVariable(scope);
}

// // helpers for stripping scope names
// static
// std::string outer_with_template(const std::string& name)
// {
// // Cut down to the outer-most scope from <name>, taking proper care of templates.
//     int tpl_open = 0;
//     for (std::string::size_type pos = 0; pos < name.size(); ++pos) {
//         std::string::value_type c = name[pos];
//
//     // count '<' and '>' to be able to skip template contents
//         if (c == '<')
//             ++tpl_open;
//         else if (c == '>')
//             --tpl_open;
//
//     // collect name up to "::"
//         else if (tpl_open == 0 && \
//                  c == ':' && pos+1 < name.size() && name[pos+1] == ':') {
//         // found the extend of the scope ... done
//             return name.substr(0, pos-1);
//         }
//     }
//
// // whole name is apparently a single scope
//     return name;
// }
//
// static
// std::string outer_no_template(const std::string& name)
// {
// // Cut down to the outer-most scope from <name>, drop templates
//     std::string::size_type first_scope = name.find(':');
//     if (first_scope == std::string::npos)
//         return name.substr(0, name.find('<'));
//     std::string::size_type first_templ = name.find('<');
//     if (first_templ == std::string::npos)
//         return name.substr(0, first_scope);
//     return name.substr(0, std::min(first_templ, first_scope));
// }
//
// #define FILL_COLL(type, filter) {                                             \
//     TIter itr{coll};                                                          \
//     type* obj = nullptr;                                                      \
//     while ((obj = (type*)itr.Next())) {                                       \
//         const char* nm = obj->GetName();                                      \
//         if (nm && nm[0] != '_' && !(obj->Property() & (filter))) {            \
//             if (gInitialNames.find(nm) == gInitialNames.end())                \
//                 cppnames.insert(nm);                                          \
//     }}}
//
// static inline
// void cond_add(Cppyy::TCppScope_t scope, const std::string& ns_scope,
//     std::set<std::string>& cppnames, const char* name, bool nofilter = false)
// {
//     if (!name || name[0] == '_' || strstr(name, ".h") != 0 || strncmp(name, "operator", 8) == 0)
//         return;
//
//     if (scope == GLOBAL_HANDLE) {
//         std::string to_add = outer_no_template(name);
//         if (nofilter || gInitialNames.find(to_add) == gInitialNames.end())
//             cppnames.insert(outer_no_template(name));
//     } else if (scope == STD_HANDLE) {
//         if (strncmp(name, "std::", 5) == 0) {
//             name += 5;
// #ifdef __APPLE__
//             if (strncmp(name, "__1::", 5) == 0) name += 5;
// #endif
//         }
//         cppnames.insert(outer_no_template(name));
//     } else {
//         if (strncmp(name, ns_scope.c_str(), ns_scope.size()) == 0)
//             cppnames.insert(outer_with_template(name + ns_scope.size()));
//     }
// }
//
// void Cppyy::GetAllCppNames(TCppScope_t scope, std::set<std::string>& cppnames)
// {
// // Collect all known names of C++ entities under scope. This is useful for IDEs
// // employing tab-completion, for example. Note that functions names need not be
// // unique as they can be overloaded.
//     TClassRef& cr = type_from_handle(scope);
//     if (scope != GLOBAL_HANDLE && !(cr.GetClass() && cr->Property()))
//         return;
//
//     std::string ns_scope = GetFinalName(scope);
//     if (scope != GLOBAL_HANDLE) ns_scope += "::";
//
// // add existing values from read rootmap files if within this scope
//     TCollection* coll = gInterpreter->GetMapfile()->GetTable();
//     {
//         TIter itr{coll};
//         TEnvRec* ev = nullptr;
//         while ((ev = (TEnvRec*)itr.Next())) {
//         // TEnv contains rootmap entries and user-side rootmap files may be already
//         // loaded on startup. Thus, filter on file name rather than load time.
//             if (gRootSOs.find(ev->GetValue()) == gRootSOs.end())
//                 cond_add(scope, ns_scope, cppnames, ev->GetName(), true);
//         }
//     }
//
// // do we care about the class table or are the rootmap and list of types enough?
// [>
//     gClassTable->Init();
//     const int N = gClassTable->Classes();
//     for (int i = 0; i < N; ++i)
//         cond_add(scope, ns_scope, cppnames, gClassTable->Next());
// */
//
// // any other types (e.g. that may have come from parsing headers)
//     coll = gROOT->GetListOfTypes();
//     {
//         TIter itr{coll};
//         TDataType* dt = nullptr;
//         while ((dt = (TDataType*)itr.Next())) {
//             if (!(dt->Property() & kIsFundamental)) {
//                 cond_add(scope, ns_scope, cppnames, dt->GetName());
//             }
//         }
//     }
//
// // add functions
//     coll = (scope == GLOBAL_HANDLE) ?
//         gROOT->GetListOfGlobalFunctions() : cr->GetListOfMethods();
//     {
//         TIter itr{coll};
//         TFunction* obj = nullptr;
//         while ((obj = (TFunction*)itr.Next())) {
//             const char* nm = obj->GetName();
//         // skip templated functions, adding only the un-instantiated ones
//             if (nm && nm[0] != '_' && strstr(nm, "<") == 0 && strncmp(nm, "operator", 8) != 0) {
//                 if (gInitialNames.find(nm) == gInitialNames.end())
//                     cppnames.insert(nm);
//             }
//         }
//     }
//
// // add uninstantiated templates
//     coll = (scope == GLOBAL_HANDLE) ?
//         gROOT->GetListOfFunctionTemplates() : cr->GetListOfFunctionTemplates();
//     FILL_COLL(TFunctionTemplate, kIsPrivate | kIsProtected)
//
// // add (global) data members
//     if (scope == GLOBAL_HANDLE) {
//         coll = gROOT->GetListOfGlobals();
//         FILL_COLL(TGlobal, kIsPrivate | kIsProtected)
//     } else {
//         coll = cr->GetListOfDataMembers();
//         FILL_COLL(TDataMember, kIsPrivate | kIsProtected)
//     }
//
// // add enums values only for user classes/namespaces
//     if (scope != GLOBAL_HANDLE && scope != STD_HANDLE) {
//         coll = cr->GetListOfEnums();
//         FILL_COLL(TEnum, kIsPrivate | kIsProtected)
//     }
//
// #ifdef __APPLE__
// // special case for Apple, add version namespace '__1' entries to std
//     if (scope == STD_HANDLE)
//         GetAllCppNames(GetScope("std::__1"), cppnames);
// #endif
// }
//
//
// // class reflection information ----------------------------------------------
std::vector<Cppyy::TCppScope_t> Cppyy::GetUsingNamespaces(TCppScope_t scope)
{
    return InterOp::GetUsingNamespaces(scope);
}

// // class reflection information ----------------------------------------------
std::string Cppyy::GetFinalName(TCppType_t klass)
{
    return InterOp::GetCompleteName(getSema(), klass);
}

std::string Cppyy::GetScopedFinalName(TCppType_t klass)
{
    return InterOp::GetQualifiedCompleteName(getSema(), klass);
}

bool Cppyy::HasVirtualDestructor(TCppScope_t scope)
{
    TCppMethod_t func = InterOp::GetDestructor(scope);
    return InterOp::IsVirtualMethod(func);
}

// bool Cppyy::HasComplexHierarchy(TCppType_t klass)
// {
//     int is_complex = 1;
//     size_t nbases = 0;
//
//     TClassRef& cr = type_from_handle(klass);
//     if (cr.GetClass() && cr->GetListOfBases() != 0)
//         nbases = GetNumBases(klass);
//
//     if (1 < nbases)
//         is_complex = 1;
//     else if (nbases == 0)
//         is_complex = 0;
//     else {         // one base class only
//         TBaseClass* base = (TBaseClass*)cr->GetListOfBases()->At(0);
//         if (base->Property() & kIsVirtualBase)
//             is_complex = 1;       // TODO: verify; can be complex, need not be.
//         else
//             is_complex = HasComplexHierarchy(GetScope(base->GetName()));
//     }
//
//     return is_complex;
// }

Cppyy::TCppIndex_t Cppyy::GetNumBases(TCppScope_t klass)
{
// Get the total number of base classes that this class has.
    return InterOp::GetNumBases(klass);
}

std::string Cppyy::GetBaseName(TCppType_t klass, TCppIndex_t ibase)
{
    return InterOp::GetName(InterOp::GetBaseClass(klass, ibase));
}

Cppyy::TCppScope_t Cppyy::GetBaseScope(TCppScope_t klass, TCppIndex_t ibase)
{
    return InterOp::GetBaseClass(klass, ibase);
}

bool Cppyy::IsSubclass(TCppScope_t derived, TCppScope_t base)
{
    return InterOp::IsSubclass(getInterp(), derived, base);
}

// bool Cppyy::IsSmartPtr(TCppType_t klass)
// {
//     TClassRef& cr = type_from_handle(klass);
//     const std::string& tn = cr->GetName();
//     if (gSmartPtrTypes.find(tn.substr(0, tn.find("<"))) != gSmartPtrTypes.end())
//         return true;
//     return false;
// }
//
// bool Cppyy::GetSmartPtrInfo(
//     const std::string& tname, TCppType_t* raw, TCppMethod_t* deref)
// {
//     const std::string& rn = ResolveName(tname);
//     if (gSmartPtrTypes.find(rn.substr(0, rn.find("<"))) != gSmartPtrTypes.end()) {
//         if (!raw && !deref) return true;
//
//         TClassRef& cr = type_from_handle(GetScope(tname));
//         if (cr.GetClass()) {
//             TFunction* func = cr->GetMethod("operator->", "");
//             if (!func) {
//                 gInterpreter->UpdateListOfMethods(cr.GetClass());
//                 func = cr->GetMethod("operator->", "");
//             }
//             if (func) {
//                if (deref) *deref = (TCppMethod_t)new_CallWrapper(func);
//                if (raw) *raw = GetScope(TClassEdit::ShortType(
//                    func->GetReturnTypeNormalizedName().c_str(), 1));
//                return (!deref || *deref) && (!raw || *raw);
//             }
//         }
//     }
//
//     return false;
// }
//
// void Cppyy::AddSmartPtrType(const std::string& type_name)
// {
//     gSmartPtrTypes.insert(ResolveName(type_name));
// }
//
// void Cppyy::AddTypeReducer(const std::string& reducable, const std::string& reduced)
// {
//     gInterpreter->AddTypeReducer(reducable, reduced);
// }


// type offsets --------------------------------------------------------------
ptrdiff_t Cppyy::GetBaseOffset(TCppScope_t derived, TCppScope_t base,
    TCppObject_t address, int direction, bool rerror)
{
    intptr_t offset = InterOp::GetBaseClassOffset(getSema(), derived, base);
    
    if (offset == -1)   // Cling error, treat silently
        return rerror ? (ptrdiff_t)offset : 0;

    return (ptrdiff_t)(direction < 0 ? -offset : offset);
}


// // method/function reflection information ------------------------------------
// Cppyy::TCppIndex_t Cppyy::GetNumMethods(TCppScope_t scope, bool accept_namespace)
// {
//     if (!accept_namespace && IsNamespace(scope))
//         return (TCppIndex_t)0;     // enforce lazy
//
//     if (scope == GLOBAL_HANDLE)
//         return gROOT->GetListOfGlobalFunctions(true)->GetSize();
//
//     TClassRef& cr = type_from_handle(scope);
//     if (cr.GetClass() && cr->GetListOfMethods(true)) {
//         Cppyy::TCppIndex_t nMethods = (TCppIndex_t)cr->GetListOfMethods(false)->GetSize();
//         if (nMethods == (TCppIndex_t)0) {
//             std::string clName = GetScopedFinalName(scope);
//             if (clName.find('<') != std::string::npos) {
//             // chicken-and-egg problem: TClass does not know about methods until
//             // instantiation, so force it
//                 std::ostringstream stmt;
//                 stmt << "template class " << clName << ";";
//                 gInterpreter->Declare(stmt.str().c_str(), true [> silent <]);
//
//             // now reload the methods
//                 return (TCppIndex_t)cr->GetListOfMethods(true)->GetSize();
//             }
//         }
//         return nMethods;
//     }
//
//     return (TCppIndex_t)0;         // unknown class?
// }

std::vector<Cppyy::TCppMethod_t> Cppyy::GetClassMethods(TCppScope_t scope)
{
    return InterOp::GetClassMethods(getSema(), scope);
}

std::vector<Cppyy::TCppScope_t> Cppyy::GetMethodsFromName(
    TCppScope_t scope, const std::string& name)
{
    return InterOp::GetFunctionsUsingName(getSema(), scope, name);
}

// Cppyy::TCppMethod_t Cppyy::GetMethod(TCppScope_t scope, TCppIndex_t idx)
// {
//     TClassRef& cr = type_from_handle(scope);
//     if (cr.GetClass()) {
//         TFunction* f = (TFunction*)cr->GetListOfMethods(false)->At((int)idx);
//         if (f) return (Cppyy::TCppMethod_t)new_CallWrapper(f);
//         return (Cppyy::TCppMethod_t)nullptr;
//     }
//
//     assert(klass == (Cppyy::TCppType_t)GLOBAL_HANDLE);
//     return (Cppyy::TCppMethod_t)idx;
// }
//
std::string Cppyy::GetMethodName(TCppMethod_t method)
{
    return InterOp::GetName(method);
}

std::string Cppyy::GetMethodFullName(TCppMethod_t method)
{
    return InterOp::GetCompleteName(getSema(), method);
}

// std::string Cppyy::GetMethodMangledName(TCppMethod_t method)
// {
//     if (method)
//         return m2f(method)->GetMangledName();
//     return "<unknown>";
// }

Cppyy::TCppType_t Cppyy::GetMethodReturnType(TCppMethod_t method)
{
    return InterOp::GetFunctionReturnType(method);
}

std::string Cppyy::GetMethodReturnTypeAsString(TCppMethod_t method)
{
    return InterOp::GetTypeAsString(
        InterOp::GetFunctionReturnType(method));
}

Cppyy::TCppIndex_t Cppyy::GetMethodNumArgs(TCppMethod_t method)
{
    return InterOp::GetFunctionNumArgs(method);
}

Cppyy::TCppIndex_t Cppyy::GetMethodReqArgs(TCppMethod_t method)
{
    return InterOp::GetFunctionRequiredArgs(method);
}

std::string Cppyy::GetMethodArgName(TCppMethod_t method, TCppIndex_t iarg)
{
    if (!method)
        return "<unknown>";

    return InterOp::GetFunctionArgName(method, iarg);
}

Cppyy::TCppType_t Cppyy::GetMethodArgType(TCppMethod_t method, TCppIndex_t iarg)
{
    return InterOp::GetFunctionArgType(method, iarg);
}

std::string Cppyy::GetMethodArgTypeAsString(TCppMethod_t method, TCppIndex_t iarg)
{
    return InterOp::GetTypeAsString(
        InterOp::GetFunctionArgType(method, iarg));
}

std::string Cppyy::GetMethodArgDefault(TCppMethod_t method, TCppIndex_t iarg)
{
    if (!method)
       return "";
    return InterOp::GetFunctionArgDefault(method, iarg);
}

std::string Cppyy::GetMethodSignature(TCppMethod_t method, bool show_formal_args, TCppIndex_t max_args)
{
    return InterOp::GetFunctionSignature(method);
}

std::string Cppyy::GetMethodPrototype(TCppMethod_t method, bool show_formal_args)
{
  assert(0 && "Unused");
  return ""; // return InterOp::GetFunctionPrototype(method, show_formal_args);
}

bool Cppyy::IsConstMethod(TCppMethod_t method)
{
    if (!method)
        return false;
    return InterOp::IsConstMethod(method);
}

// Cppyy::TCppIndex_t Cppyy::GetNumTemplatedMethods(TCppScope_t scope, bool accept_namespace)
// {
//     if (!accept_namespace && IsNamespace(scope))
//         return (TCppIndex_t)0;     // enforce lazy
//
//     if (scope == GLOBAL_HANDLE) {
//         TCollection* coll = gROOT->GetListOfFunctionTemplates();
//         if (coll) return (TCppIndex_t)coll->GetSize();
//     } else {
//         TClassRef& cr = type_from_handle(scope);
//         if (cr.GetClass()) {
//             TCollection* coll = cr->GetListOfFunctionTemplates(true);
//             if (coll) return (TCppIndex_t)coll->GetSize();
//         }
//     }
//
// // failure ...
//     return (TCppIndex_t)0;
// }
//
// std::string Cppyy::GetTemplatedMethodName(TCppScope_t scope, TCppIndex_t imeth)
// {
//     if (scope == (TCppScope_t)GLOBAL_HANDLE)
//         return ((THashList*)gROOT->GetListOfFunctionTemplates())->At((int)imeth)->GetName();
//     else {
//         TClassRef& cr = type_from_handle(scope);
//         if (cr.GetClass())
//             return cr->GetListOfFunctionTemplates(false)->At((int)imeth)->GetName();
//     }
//
// // failure ...
//     assert(!"should not be called unless GetNumTemplatedMethods() succeeded");
//     return "";
// }
//
// bool Cppyy::IsTemplatedConstructor(TCppScope_t scope, TCppIndex_t imeth)
// {
//     if (scope == (TCppScope_t)GLOBAL_HANDLE)
//         return false;
//
//     TClassRef& cr = type_from_handle(scope);
//     if (cr.GetClass()) {
//         TFunctionTemplate* f = (TFunctionTemplate*)cr->GetListOfFunctionTemplates(false)->At((int)imeth);
//         return f->ExtraProperty() & kIsConstructor;
//     }
//
//     return false;
// }
//
bool Cppyy::ExistsMethodTemplate(TCppScope_t scope, const std::string& name)
{
    return InterOp::ExistsFunctionTemplate(getSema(), name, scope);
}

bool Cppyy::IsTemplatedMethod(TCppMethod_t method)
{
    return InterOp::IsTemplatedFunction(method);
}

// // helpers for Cppyy::GetMethodTemplate()
// static std::map<TDictionary::DeclId_t, CallWrapper*> gMethodTemplates;
//
// static inline
// void remove_space(std::string& n) {
//    std::string::iterator pos = std::remove_if(n.begin(), n.end(), isspace);
//    n.erase(pos, n.end());
// }
//
// static inline
// bool template_compare(std::string n1, std::string n2) {
//     if (n1.back() == '>') n1 = n1.substr(0, n1.size()-1);
//     remove_space(n1);
//     remove_space(n2);
//     return n2.compare(0, n1.size(), n1) == 0;
// }
//
Cppyy::TCppMethod_t Cppyy::GetMethodTemplate(
    TCppScope_t scope, const std::string& name, const std::string& proto)
{
#ifdef PRINT_DEBUG
    printf("========== GMT: %s; %s\n", name.c_str(), proto.c_str());
#endif
// // There is currently no clean way of extracting a templated method out of ROOT/meta
// // for a variety of reasons, none of them fundamental. The game played below is to
// // first get any pre-existing functions already managed by ROOT/meta, but if that fails,
// // to do an explicit lookup that ignores the prototype (i.e. the full name should be
// // enough), and finally to ignore the template arguments part of the name as this fails
// // in cling if there are default parameters.
//     TFunction* func = nullptr; ClassInfo_t* cl = nullptr;
//     if (scope == (cppyy_scope_t)GLOBAL_HANDLE) {
//         func = gROOT->GetGlobalFunctionWithPrototype(name.c_str(), proto.c_str());
//         if (func && name.back() == '>') {
//         // make sure that all template parameters match (more are okay, e.g. defaults or
//         // ones derived from the arguments or variadic templates)
//             if (!template_compare(name, func->GetName()))
//                 func = nullptr;  // happens if implicit conversion matches the overload
//         }
//     } else {
//         TClassRef& cr = type_from_handle(scope);
//         if (cr.GetClass()) {
//             func = cr->GetMethodWithPrototype(name.c_str(), proto.c_str());
//             if (!func) {
//                 cl = cr->GetClassInfo();
//             // try base classes to cover a common 'using' case (TODO: this is stupid and misses
//             // out on base classes; fix that with improved access to Cling)
//                 TCppIndex_t nbases = GetNumBases(scope);
//                 for (TCppIndex_t i = 0; i < nbases; ++i) {
//                     TClassRef& base = type_from_handle(GetScope(GetBaseName(scope, i)));
//                     if (base.GetClass()) {
//                         func = base->GetMethodWithPrototype(name.c_str(), proto.c_str());
//                         if (func) break;
//                     }
//                 }
//             }
//         }
//     }
//
//     if (!func && name.back() == '>' && (cl || scope == (cppyy_scope_t)GLOBAL_HANDLE)) {
//     // try again, ignoring proto in case full name is complete template
//         auto declid = gInterpreter->GetFunction(cl, name.c_str());
//         if (declid) {
//              auto existing = gMethodTemplates.find(declid);
//              if (existing == gMethodTemplates.end()) {
//                  auto cw = new_CallWrapper(declid, name);
//                  existing = gMethodTemplates.insert(std::make_pair(declid, cw)).first;
//              }
//              return (TCppMethod_t)existing->second;
//         }
//     }
//
//     if (func) {
//     // make sure we didn't match a non-templated overload
//         if (func->ExtraProperty() & kIsTemplateSpec)
//             return (TCppMethod_t)new_CallWrapper(func);
//
//     // disregard this non-templated method as it will be considered when appropriate
//         return (TCppMethod_t)nullptr;
//     }
//
// // try again with template arguments removed from name, if applicable
//     if (name.back() == '>') {
//         auto pos = name.find('<');
//         if (pos != std::string::npos) {
//             TCppMethod_t cppmeth = GetMethodTemplate(scope, name.substr(0, pos), proto);
//             if (cppmeth) {
//             // allow if requested template names match up to the result
//                 const std::string& alt = GetMethodFullName(cppmeth);
//                 if (name.size() < alt.size() && alt.find('<') == pos) {
//                     if (template_compare(name, alt))
//                         return cppmeth;
//                 }
//             }
//         }
//     }

// failure ...
    return (TCppMethod_t)nullptr;
}
//
// static inline
// std::string type_remap(const std::string& n1, const std::string& n2)
// {
// // Operator lookups of (C++ string, Python str) should succeed for the combos of
// // string/str, wstring/str, string/unicode and wstring/unicode; since C++ does not have a
// // operator+(std::string, std::wstring), we'll have to look up the same type and rely on
// // the converters in CPyCppyy/_cppyy.
//     if (n1 == "str" || n1 == "unicode") {
//         if (n2 == "std::basic_string<wchar_t,std::char_traits<wchar_t>,std::allocator<wchar_t> >")
//             return n2;                      // match like for like
//         return "std::string";               // probably best bet
//     } else if (n1 == "float") {
//         return "double";                    // debatable, but probably intended
//     } else if (n1 == "complex") {
//         return "std::complex<double>";
//     }
//     return n1;
// }
//
// Cppyy::TCppIndex_t Cppyy::GetGlobalOperator(
//     TCppType_t scope, const std::string& lc, const std::string& rc, const std::string& opname)
// {
// // Find a global operator function with a matching signature; prefer by-ref, but
// // fall back on by-value if that fails.
//     std::string lcname1 = TClassEdit::CleanType(lc.c_str());
//     const std::string& rcname = rc.empty() ? rc : type_remap(TClassEdit::CleanType(rc.c_str()), lcname1);
//     const std::string& lcname = type_remap(lcname1, rcname);
//
//     std::string proto = lcname + "&" + (rc.empty() ? rc : (", " + rcname + "&"));
//     if (scope == (cppyy_scope_t)GLOBAL_HANDLE) {
//         TFunction* func = gROOT->GetGlobalFunctionWithPrototype(opname.c_str(), proto.c_str());
//         if (func) retIsDatamemberPresent>GetGlobalFunctionWithPrototype(opname.c_str(), proto.c_str());
//         if (func) return (TCppIndex_t)new_CallWrapper(func);
//     } else {
//         TClassRef& cr = type_from_handle(scope);
//         if (cr.GetClass()) {
//             TFunction* func = cr->GetMethodWithPrototype(opname.c_str(), proto.c_str());
//             if (func) return (TCppIndex_t)cr->GetListOfMethods()->IndexOf(func);
//             proto = lcname + (rc.empty() ? rc : (", " + rcname));
//             func = cr->GetMethodWithPrototype(opname.c_str(), proto.c_str());
//             if (func) return (TCppIndex_t)cr->GetListOfMethods()->IndexOf(func);
//         }
//     }
//
// // failure ...
//     return (TCppIndex_t)-1;
// }
//
// // method properties ---------------------------------------------------------
bool Cppyy::IsPublicMethod(TCppMethod_t method)
{
    return InterOp::IsPublicMethod(method);
}

bool Cppyy::IsProtectedMethod(TCppMethod_t method)
{
    return InterOp::IsProtectedMethod(method);
}

bool Cppyy::IsPrivateMethod(TCppMethod_t method)
{
    return InterOp::IsPrivateMethod(method);
}

bool Cppyy::IsConstructor(TCppMethod_t method)
{
    return InterOp::IsConstructor(method);
}

bool Cppyy::IsDestructor(TCppMethod_t method)
{
    return InterOp::IsDestructor(method);
}

bool Cppyy::IsStaticMethod(TCppMethod_t method)
{
    return InterOp::IsStaticMethod(method);
}

//
// // data member reflection information ----------------------------------------
// Cppyy::TCppIndex_t Cppyy::GetNumDatamembers(TCppScope_t scope, bool accept_namespace)
// {
//     if (!accept_namespace && IsNamespace(scope))
//         return (TCppIndex_t)0;     // enforce lazy
//
//     if (scope == GLOBAL_HANDLE)
//         return gROOT->GetListOfGlobals(true)->GetSize();
//
//     TClassRef& cr = type_from_handle(scope);
//     if (cr.GetClass() && cr->GetListOfDataMembers())
//         return cr->GetListOfDataMembers()->GetSize();
//
//     return (TCppIndex_t)0;         // unknown class?
// }

std::vector<Cppyy::TCppScope_t> Cppyy::GetDatamembers(TCppScope_t scope)
{
    return InterOp::GetDatamembers(scope);
}

bool Cppyy::CheckDatamember(TCppScope_t scope, const std::string& name) {
    return (bool) InterOp::LookupDatamember(getSema(), name, scope);
}

// std::string Cppyy::GetDatamemberName(TCppScope_t scope, TCppIndex_t idata)
// {
//     TClassRef& cr = type_from_handle(scope);
//     if (cr.GetClass()) {
//         TDataMember* m = (TDataMember*)cr->GetListOfDataMembers()->At((int)idata);
//         return m->GetName();
//     }
//     assert(scope == GLOBAL_HANDLE);
//     TGlobal* gbl = g_globalvars[idata];
//     return gbl->GetName();
// }
//
// static inline
// int count_scopes(const std::string& tpname)
// {
//     int count = 0;
//     std::string::size_type pos = tpname.find("::", 0);
//     while (pos != std::string::npos) {
//         count++;
//         pos = tpname.find("::", pos+1);
//     }
//     return count;
// }

Cppyy::TCppType_t Cppyy::GetDatamemberType(TCppScope_t var)
{
    return InterOp::GetVariableType(var);
}

std::string Cppyy::GetDatamemberTypeAsString(TCppScope_t scope)
{
    return InterOp::GetTypeAsString(
        InterOp::GetVariableType(scope));
}

std::string Cppyy::GetTypeAsString(TCppType_t type)
{
    return InterOp::GetTypeAsString(type);
}

intptr_t Cppyy::GetDatamemberOffset(TCppScope_t var)
{
    return InterOp::GetVariableOffset(getInterp(), var);
}

// static inline
// Cppyy::TCppIndex_t gb2idx(TGlobal* gb)
// {
//     if (!gb) return (Cppyy::TCppIndex_t)-1;
//
//     auto pidx = g_globalidx.find(gb);
//     if (pidx == g_globalidx.end()) {
//         auto idx = g_globalvars.size();
//         g_globalvars.push_back(gb);
//         g_globalidx[gb] = idx;
//         return (Cppyy::TCppIndex_t)idx;
//     }
//     return (Cppyy::TCppIndex_t)pidx->second;
// }
//
// Cppyy::TCppIndex_t Cppyy::GetDatamemberIndex(TCppScope_t scope, const std::string& name)
// {
//     if (scope == GLOBAL_HANDLE) {
//         TGlobal* gb = (TGlobal*)gROOT->GetListOfGlobals(false [> load <])->FindObject(name.c_str());
//         if (!gb) gb = (TGlobal*)gROOT->GetListOfGlobals(true  [> load <])->FindObject(name.c_str());
//         if (!gb) {
//         // some enums are not loaded as they are not considered part of
//         // the global scope, but of the enum scope; get them w/o checking
//             TDictionary::DeclId_t did = gInterpreter->GetDataMember(nullptr, name.c_str());
//             if (did) {
//                 DataMemberInfo_t* t = gInterpreter->DataMemberInfo_Factory(did, nullptr);
//                 ((TListOfDataMembers*)gROOT->GetListOfGlobals())->Get(t, true);
//                 gb = (TGlobal*)gROOT->GetListOfGlobals(false [> load <])->FindObject(name.c_str());
//             }
//         }
//
//         if (gb && strcmp(gb->GetFullTypeName(), "(lambda)") == 0) {
//         // lambdas use a compiler internal closure type, so we wrap
//         // them, then return the wrapper's type
//         // TODO: this current leaks the std::function; also, if possible,
//         //       should instantiate through TClass rather then ProcessLine
//             std::ostringstream s;
//             s << "auto __cppyy_internal_wrap_" << name << " = "
//                  "new __cling_internal::FT<decltype(" << name << ")>::F"
//                  "{" << name << "};";
//             gInterpreter->ProcessLine(s.str().c_str());
//             TGlobal* wrap = (TGlobal*)gROOT->GetListOfGlobals(true)->FindObject(
//                 ("__cppyy_internal_wrap_"+name).c_str());
//             if (wrap && wrap->GetAddress()) gb = wrap;
//         }
//
//         return gb2idx(gb);
//
//     } else {
//         TClassRef& cr = type_from_handle(scope);
//         if (cr.GetClass()) {
//             TDataMember* dm =
//                 (TDataMember*)cr->GetListOfDataMembers()->FindObject(name.c_str());
//             // TODO: turning this into an index is silly ...
//             if (dm) return (TCppIndex_t)cr->GetListOfDataMembers()->IndexOf(dm);
//         }
//     }
//
//     return (TCppIndex_t)-1;
// }
//
// Cppyy::TCppIndex_t Cppyy::GetDatamemberIndexEnumerated(TCppScope_t scope, TCppIndex_t idata)
// {
//     if (scope == GLOBAL_HANDLE) {
//         TGlobal* gb = (TGlobal*)((THashList*)gROOT->GetListOfGlobals(false [> load <]))->At((int)idata);
//         return gb2idx(gb);
//     }
//
//     return idata;
// }

// data member properties ----------------------------------------------------
bool Cppyy::IsPublicData(TCppScope_t datamem)
{
    return InterOp::IsPublicVariable(datamem);
}

bool Cppyy::IsProtectedData(TCppScope_t datamem)
{
    return InterOp::IsProtectedVariable(datamem);
}

bool Cppyy::IsPrivateData(TCppScope_t datamem)
{
    return InterOp::IsPrivateVariable(datamem);
}

bool Cppyy::IsStaticDatamember(TCppScope_t var)
{
    return InterOp::IsStaticVariable(var);
}

bool Cppyy::IsConstVar(TCppScope_t var)
{
    return InterOp::IsConstVariable(var);
}

// bool Cppyy::IsEnumData(TCppScope_t scope, TCppIndex_t idata)
// {
// // TODO: currently, ROOT/meta does not properly distinguish between variables of enum
// // type, and values of enums. The latter are supposed to be const. This code relies on
// // odd features (bugs?) to figure out the difference, but this should really be fixed
// // upstream and/or deserves a new API.

//     if (scope == GLOBAL_HANDLE) {
//         TGlobal* gbl = g_globalvars[idata];

//     // make use of an oddity: enum global variables do not have their kIsStatic bit
//     // set, whereas enum global values do
//         return (gbl->Property() & kIsEnum) && (gbl->Property() & kIsStatic);
//     }

//     TClassRef& cr = type_from_handle(scope);
//     if (cr.GetClass()) {
//         TDataMember* m = (TDataMember*)cr->GetListOfDataMembers()->At((int)idata);
//         std::string ti = m->GetTypeName();

//     // can't check anonymous enums by type name, so just accept them as enums
//         if (ti.rfind("(anonymous)") != std::string::npos)
//             return m->Property() & kIsEnum;

//     // since there seems to be no distinction between data of enum type and enum values,
//     // check the list of constants for the type to see if there's a match
//         if (ti.rfind(cr->GetName(), 0) != std::string::npos) {
//             std::string::size_type s = strlen(cr->GetName())+2;
//             if (s < ti.size()) {
//                 TEnum* ee = ((TListOfEnums*)cr->GetListOfEnums())->GetObject(ti.substr(s, std::string::npos).c_str());
//                 if (ee) return ee->GetConstant(m->GetName());
//             }
//         }
//     }

// // this default return only means that the data will be writable, not that it will
// // be unreadable or otherwise misrepresented
//     return false;
// }

std::vector<long int>  Cppyy::GetDimensions(TCppType_t type)
{
    return InterOp::GetDimensions(type);
}

// enum properties -----------------------------------------------------------
std::vector<Cppyy::TCppScope_t> Cppyy::GetEnumConstants(TCppScope_t scope)
{
    return InterOp::GetEnumConstants(scope);
}

Cppyy::TCppType_t Cppyy::GetEnumConstantType(TCppScope_t scope)
{
    return InterOp::GetEnumConstantType(scope);
}

Cppyy::TCppIndex_t Cppyy::GetEnumDataValue(TCppScope_t scope)
{
    return InterOp::GetEnumConstantValue(scope);
}

// std::string Cppyy::GetEnumDataName(TCppEnum_t etype, TCppIndex_t idata)
// {
//     return ((TEnumConstant*)((TEnum*)etype)->GetConstants()->At((int)idata))->GetName();
// }
//
// long long Cppyy::GetEnumDataValue(TCppEnum_t etype, TCppIndex_t idata)
// {
//      TEnumConstant* ecst = (TEnumConstant*)((TEnum*)etype)->GetConstants()->At((int)idata);
//      return (long long)ecst->GetValue();
// }

Cppyy::TCppScope_t Cppyy::InstantiateTemplateClass(
             TCppScope_t tmpl, InterOp::TemplateArgInfo* args, size_t args_size)
{
    return InterOp::InstantiateClassTemplate(getInterp(), tmpl, args,
                                             args_size);
}

void Cppyy::DumpScope(TCppScope_t scope)
{
    InterOp::DumpScope(scope);
}

//- C-linkage wrappers -------------------------------------------------------

extern "C" {
// direct interpreter access ----------------------------------------------
int cppyy_compile(const char* code) {
    return Cppyy::Compile(code);
}

int cppyy_compile_silent(const char* code) {
    return Cppyy::Compile(code, true /* silent */);
}

char* cppyy_to_string(cppyy_type_t klass, cppyy_object_t obj) {
    return cppstring_to_cstring(Cppyy::ToString((Cppyy::TCppType_t) klass, obj));
}


// name to opaque C++ scope representation --------------------------------
// char* cppyy_resolve_name(const char* cppitem_name) {
//     return cppstring_to_cstring(Cppyy::ResolveName(cppitem_name));
// }

// char* cppyy_resolve_enum(const char* enum_type) {
//     return cppstring_to_cstring(Cppyy::ResolveEnum(enum_type));
// }

cppyy_scope_t cppyy_get_scope(const char* scope_name) {
    return cppyy_scope_t(Cppyy::GetScope(scope_name));
}
//
// cppyy_type_t cppyy_actual_class(cppyy_type_t klass, cppyy_object_t obj) {
//     return cppyy_type_t(Cppyy::GetActualClass(klass, (void*)obj));
// }

size_t cppyy_size_of_klass(cppyy_type_t klass) {
    return Cppyy::SizeOf((Cppyy::TCppType_t) klass);
}

// size_t cppyy_size_of_type(const char* type_name) {
//     return Cppyy::SizeOf(type_name);
// }
//
// int cppyy_is_builtin(const char* type_name) {
//     return (int)Cppyy::IsBuiltin(type_name);
// }
//
// int cppyy_is_complete(const char* type_name) {
//     return (int)Cppyy::IsComplete(type_name);
// }
//
//
// [> memory management ------------------------------------------------------ <]
// cppyy_object_t cppyy_allocate(cppyy_type_t type) {
//     return cppyy_object_t(Cppyy::Allocate(type));
// }
//
// void cppyy_deallocate(cppyy_type_t type, cppyy_object_t self) {
//     Cppyy::Deallocate(type, (void*)self);
// }
//
// cppyy_object_t cppyy_construct(cppyy_type_t type) {
//     return (cppyy_object_t)Cppyy::Construct(type);
// }
//
// void cppyy_destruct(cppyy_type_t type, cppyy_object_t self) {
//     Cppyy::Destruct(type, (void*)self);
// }
//
//
// [> method/function dispatching -------------------------------------------- <]
// [> Exception types:
//     1: default (unknown exception)
//     2: standard exception
// */
// #define CPPYY_HANDLE_EXCEPTION                                               \
//     catch (std::exception& e) {                                              \
//         cppyy_exctype_t* etype = (cppyy_exctype_t*)((Parameter*)args+nargs); \
//         *etype = (cppyy_exctype_t)2;                                         \
//         *((char**)(etype+1)) = cppstring_to_cstring(e.what());               \
//     }                                                                        \
//     catch (...) {                                                            \
//         cppyy_exctype_t* etype = (cppyy_exctype_t*)((Parameter*)args+nargs); \
//         *etype = (cppyy_exctype_t)1;                                         \
//         *((char**)(etype+1)) =                                               \
//             cppstring_to_cstring("unhandled, unknown C++ exception");        \
//     }
//
// void cppyy_call_v(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         Cppyy::CallV(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
// }
//
// unsigned char cppyy_call_b(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (unsigned char)Cppyy::CallB(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (unsigned char)-1;
// }
//
// char cppyy_call_c(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (char)Cppyy::CallC(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (char)-1;
// }
//
// short cppyy_call_h(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (short)Cppyy::CallH(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (short)-1;
// }
//
// int cppyy_call_i(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (int)Cppyy::CallI(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (int)-1;
// }
//
// long cppyy_call_l(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (long)Cppyy::CallL(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (long)-1;
// }
//
// long long cppyy_call_ll(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (long long)Cppyy::CallLL(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (long long)-1;
// }
//
// float cppyy_call_f(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (float)Cppyy::CallF(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (float)-1;
// }
//
// double cppyy_call_d(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (double)Cppyy::CallD(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (double)-1;
// }
//
// long double cppyy_call_ld(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (long double)Cppyy::CallLD(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (long double)-1;
// }
//
// double cppyy_call_nld(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     return (double)cppyy_call_ld(method, self, nargs, args);
// }
//
// void* cppyy_call_r(cppyy_method_t method, cppyy_object_t self, int nargs, void* args) {
//     try {
//         return (void*)Cppyy::CallR(method, (void*)self, nargs, args);
//     } CPPYY_HANDLE_EXCEPTION
//     return (void*)nullptr;
// }
//
// char* cppyy_call_s(
//         cppyy_method_t method, cppyy_object_t self, int nargs, void* args, size_t* lsz) {
//     try {
//         return Cppyy::CallS(method, (void*)self, nargs, args, lsz);
//     } CPPYY_HANDLE_EXCEPTION
//     return (char*)nullptr;
// }
//
// cppyy_object_t cppyy_constructor(
//         cppyy_method_t method, cppyy_type_t klass, int nargs, void* args) {
//     try {
//         return cppyy_object_t(Cppyy::CallConstructor(method, klass, nargs, args));
//     } CPPYY_HANDLE_EXCEPTION
//     return (cppyy_object_t)0;
// }
//
// void cppyy_destructor(cppyy_type_t klass, cppyy_object_t self) {
//     Cppyy::CallDestructor(klass, self);
// }
//
// cppyy_object_t cppyy_call_o(cppyy_method_t method, cppyy_object_t self,
//         int nargs, void* args, cppyy_type_t result_type) {
//     try {
//         return cppyy_object_t(Cppyy::CallO(method, (void*)self, nargs, args, result_type));
//     } CPPYY_HANDLE_EXCEPTION
//     return (cppyy_object_t)0;
// }
//
// cppyy_funcaddr_t cppyy_function_address(cppyy_method_t method) {
//     return cppyy_funcaddr_t(Cppyy::GetFunctionAddress(method, true));
// }
//
//
// [> handling of function argument buffer ----------------------------------- <]
// void* cppyy_allocate_function_args(int nargs) {
// // for calls through C interface, require extra space for reporting exceptions
//     return malloc(nargs*sizeof(Parameter)+sizeof(cppyy_exctype_t)+sizeof(char**));
// }
//
// void cppyy_deallocate_function_args(void* args) {
//     free(args);
// }
//
// size_t cppyy_function_arg_sizeof() {
//     return (size_t)Cppyy::GetFunctionArgSizeof();
// }
//
// size_t cppyy_function_arg_typeoffset() {
//     return (size_t)Cppyy::GetFunctionArgTypeoffset();
// }


// scope reflection information ------------------------------------------- 
int cppyy_is_namespace(cppyy_scope_t scope) {
    return (int)Cppyy::IsNamespace((Cppyy::TCppScope_t) scope);
}

// int cppyy_is_template(const char* template_name) {
//     return (int)Cppyy::IsTemplate(template_name);
// }
//
// int cppyy_is_abstract(cppyy_type_t type) {
//     return (int)Cppyy::IsAbstract(type);
// }
//
// int cppyy_is_enum(const char* type_name) {
//     return (int)Cppyy::IsEnum(type_name);
// }
//
// int cppyy_is_aggregate(cppyy_type_t type) {
//     return (int)Cppyy::IsAggregate(type);
// }
//
// int cppyy_is_default_constructable(cppyy_type_t type) {
//     return (int)Cppyy::IsDefaultConstructable(type);
// }
//
// const char** cppyy_get_all_cpp_names(cppyy_scope_t scope, size_t* count) {
//     std::set<std::string> cppnames;
//     Cppyy::GetAllCppNames(scope, cppnames);
//     const char** c_cppnames = (const char**)malloc(cppnames.size()*sizeof(const char*));
//     int i = 0;
//     for (const auto& name : cppnames) {
//         c_cppnames[i] = cppstring_to_cstring(name);
//         ++i;
//     }
//     *count = cppnames.size();
//     return c_cppnames;
// }
//
//
// [> namespace reflection information --------------------------------------- <]
// cppyy_scope_t* cppyy_get_using_namespaces(cppyy_scope_t scope) {
//     const std::vector<Cppyy::TCppScope_t>& uv = Cppyy::GetUsingNamespaces((Cppyy::TCppScope_t)scope);
//
//     if (uv.empty())
//         return (cppyy_index_t*)nullptr;
//
//     cppyy_scope_t* llresult = (cppyy_scope_t*)malloc(sizeof(cppyy_scope_t)*(uv.size()+1));
//     for (int i = 0; i < (int)uv.size(); ++i) llresult[i] = uv[i];
//     llresult[uv.size()] = (cppyy_scope_t)0;
//     return llresult;
// }
//
//
// [> class reflection information ------------------------------------------- <]
// char* cppyy_final_name(cppyy_type_t type) {
//     return cppstring_to_cstring(Cppyy::GetFinalName(type));
// }
//
// char* cppyy_scoped_final_name(cppyy_type_t type) {
//     return cppstring_to_cstring(Cppyy::GetScopedFinalName(type));
// }
//
// int cppyy_has_virtual_destructor(cppyy_type_t type) {
//     return (int)Cppyy::HasVirtualDestructor(type);
// }
//
// int cppyy_has_complex_hierarchy(cppyy_type_t type) {
//     return (int)Cppyy::HasComplexHierarchy(type);
// }
//
// int cppyy_num_bases(cppyy_type_t type) {
//     return (int)Cppyy::GetNumBases(type);
// }
//
// char* cppyy_base_name(cppyy_type_t type, int base_index) {
//     return cppstring_to_cstring(Cppyy::GetBaseName (type, base_index));
// }
//
// int cppyy_is_subtype(cppyy_type_t derived, cppyy_type_t base) {
//     return (int)Cppyy::IsSubclass(derived, base);
// }
//
// int cppyy_is_smartptr(cppyy_type_t type) {
//     return (int)Cppyy::IsSmartPtr(type);
// }
//
// int cppyy_smartptr_info(const char* name, cppyy_type_t* raw, cppyy_method_t* deref) {
//     return (int)Cppyy::GetSmartPtrInfo(name, raw, deref);
// }
//
// void cppyy_add_smartptr_type(const char* type_name) {
//     Cppyy::AddSmartPtrType(type_name);
// }
//
// void cppyy_add_type_reducer(const char* reducable, const char* reduced) {
//     Cppyy::AddTypeReducer(reducable, reduced);
// }
//
//
// [> calculate offsets between declared and actual type, up-cast: direction > 0; down-cast: direction < 0 <]
// ptrdiff_t cppyy_base_offset(cppyy_type_t derived, cppyy_type_t base, cppyy_object_t address, int direction) {
//     return (ptrdiff_t)Cppyy::GetBaseOffset(derived, base, (void*)address, direction, 0);
// }
//
//
// [> method/function reflection information --------------------------------- <]
// int cppyy_num_methods(cppyy_scope_t scope) {
//     return (int)Cppyy::GetNumMethods(scope);
// }
//
// int cppyy_num_methods_ns(cppyy_scope_t scope) {
//     return (int)Cppyy::GetNumMethods(scope, true);
// }
//
// cppyy_index_t* cppyy_method_indices_from_name(cppyy_scope_t scope, const char* name)
// {
//     std::vector<cppyy_index_t> result = Cppyy::GetMethodIndicesFromName(scope, name);
//
//     if (result.empty())
//         return (cppyy_index_t*)nullptr;
//
//     cppyy_index_t* llresult = (cppyy_index_t*)malloc(sizeof(cppyy_index_t)*(result.size()+1));
//     for (int i = 0; i < (int)result.size(); ++i) llresult[i] = result[i];
//     llresult[result.size()] = -1;
//     return llresult;
// }
//
// cppyy_method_t cppyy_get_method(cppyy_scope_t scope, cppyy_index_t idx) {
//     return cppyy_method_t(Cppyy::GetMethod(scope, idx));
// }
//
// char* cppyy_method_name(cppyy_method_t method) {
//     return cppstring_to_cstring(Cppyy::GetMethodName((Cppyy::TCppMethod_t)method));
// }
//
// char* cppyy_method_full_name(cppyy_method_t method) {
//     return cppstring_to_cstring(Cppyy::GetMethodFullName((Cppyy::TCppMethod_t)method));
// }
//
// char* cppyy_method_mangled_name(cppyy_method_t method) {
//     return cppstring_to_cstring(Cppyy::GetMethodMangledName((Cppyy::TCppMethod_t)method));
// }
//
// char* cppyy_method_result_type(cppyy_method_t method) {
//     return cppstring_to_cstring(Cppyy::GetMethodReturnTypeAsString((Cppyy::TCppMethod_t)method));
// }
//
// int cppyy_method_num_args(cppyy_method_t method) {
//     return (int)Cppyy::GetMethodNumArgs((Cppyy::TCppMethod_t)method);
// }
//
// int cppyy_method_req_args(cppyy_method_t method) {
//     return (int)Cppyy::GetMethodReqArgs((Cppyy::TCppMethod_t)method);
// }
//
// char* cppyy_method_arg_name(cppyy_method_t method, int arg_index) {
//     return cppstring_to_cstring(Cppyy::GetMethodArgName((Cppyy::TCppMethod_t)method, (Cppyy::TCppIndex_t)arg_index));
// }
//
// char* cppyy_method_arg_type(cppyy_method_t method, int arg_index) {
//     return cppstring_to_cstring(Cppyy::GetMethodArgType((Cppyy::TCppMethod_t)method, (Cppyy::TCppIndex_t)arg_index));
// }
//
// char* cppyy_method_arg_default(cppyy_method_t method, int arg_index) {
//     return cppstring_to_cstring(Cppyy::GetMethodArgDefault((Cppyy::TCppMethod_t)method, (Cppyy::TCppIndex_t)arg_index));
// }
//
// char* cppyy_method_signature(cppyy_method_t method, int show_formalargs) {
//     return cppstring_to_cstring(Cppyy::GetMethodSignature((Cppyy::TCppMethod_t)method, (bool)show_formalargs));
// }
//
// char* cppyy_method_signature_max(cppyy_method_t method, int show_formalargs, int maxargs) {
//     return cppstring_to_cstring(Cppyy::GetMethodSignature((Cppyy::TCppMethod_t)method, (bool)show_formalargs, (Cppyy::TCppIndex_t)maxargs));
// }
//
// char* cppyy_method_prototype(cppyy_scope_t scope, cppyy_method_t method, int show_formalargs) {
//     return cppstring_to_cstring(Cppyy::GetMethodPrototype(
//         (Cppyy::TCppScope_t)scope, (Cppyy::TCppMethod_t)method, (bool)show_formalargs));
// }
//
// int cppyy_is_const_method(cppyy_method_t method) {
//     return (int)Cppyy::IsConstMethod((Cppyy::TCppMethod_t)method);
// }
//
// int cppyy_get_num_templated_methods(cppyy_scope_t scope) {
//     return (int)Cppyy::GetNumTemplatedMethods((Cppyy::TCppScope_t)scope);
// }
//
// int cppyy_get_num_templated_methods_ns(cppyy_scope_t scope) {
//     return (int)Cppyy::GetNumTemplatedMethods((Cppyy::TCppScope_t)scope, true);
// }
//
// char* cppyy_get_templated_method_name(cppyy_scope_t scope, cppyy_index_t imeth) {
//     return cppstring_to_cstring(Cppyy::GetTemplatedMethodName((Cppyy::TCppScope_t)scope, (Cppyy::TCppIndex_t)imeth));
// }
//
// int cppyy_is_templated_constructor(cppyy_scope_t scope, cppyy_index_t imeth) {
//     return Cppyy::IsTemplatedConstructor((Cppyy::TCppScope_t)scope, (Cppyy::TCppIndex_t)imeth);
// }
//
// int cppyy_exists_method_template(cppyy_scope_t scope, const char* name) {
//     return (int)Cppyy::ExistsMethodTemplate((Cppyy::TCppScope_t)scope, name);
// }
//
// int cppyy_method_is_template(cppyy_scope_t scope, cppyy_index_t idx) {
//     return (int)Cppyy::IsMethodTemplate((Cppyy::TCppScope_t)scope, idx);
// }
//
// cppyy_method_t cppyy_get_method_template(cppyy_scope_t scope, const char* name, const char* proto) {
//     return cppyy_method_t(Cppyy::GetMethodTemplate((Cppyy::TCppScope_t)scope, name, proto));
// }
//
// cppyy_index_t cppyy_get_global_operator(cppyy_scope_t scope, cppyy_scope_t lc, cppyy_scope_t rc, const char* op) {
//     return cppyy_index_t(Cppyy::GetGlobalOperator(scope, Cppyy::GetScopedFinalName(lc), Cppyy::GetScopedFinalName(rc), op));
// }
//
//
// [> method properties ------------------------------------------------------ <]
// int cppyy_is_publicmethod(cppyy_method_t method) {
//     return (int)Cppyy::IsPublicMethod((Cppyy::TCppMethod_t)method);
// }
//
// int cppyy_is_protectedmethod(cppyy_method_t method) {
//     return (int)Cppyy::IsProtectedMethod((Cppyy::TCppMethod_t)method);
// }
//
// int cppyy_is_constructor(cppyy_method_t method) {
//     return (int)Cppyy::IsConstructor((Cppyy::TCppMethod_t)method);
// }
//
// int cppyy_is_destructor(cppyy_method_t method) {
//     return (int)Cppyy::IsDestructor((Cppyy::TCppMethod_t)method);
// }
//
// int cppyy_is_staticmethod(cppyy_method_t method) {
//     return (int)Cppyy::IsStaticMethod((Cppyy::TCppMethod_t)method);
// }
//
//
// [> data member reflection information ------------------------------------- <]
// int cppyy_num_datamembers(cppyy_scope_t scope) {
//     return (int)Cppyy::GetNumDatamembers(scope);
// }
//
// int cppyy_num_datamembers_ns(cppyy_scope_t scope) {
//     return (int)Cppyy::GetNumDatamembers(scope, true);
// }
//
// char* cppyy_datamember_name(cppyy_scope_t scope, int datamember_index) {
//     return cppstring_to_cstring(Cppyy::GetDatamemberName(scope, datamember_index));
// }
//
// char* cppyy_datamember_type(cppyy_scope_t scope, int datamember_index) {
//     return cppstring_to_cstring(Cppyy::GetDatamemberType(scope, datamember_index));
// }
//
// intptr_t cppyy_datamember_offset(cppyy_scope_t scope, int datamember_index) {
//     return intptr_t(Cppyy::GetDatamemberOffset(scope, datamember_index));
// }
//
// int cppyy_datamember_index(cppyy_scope_t scope, const char* name) {
//     return (int)Cppyy::GetDatamemberIndex(scope, name);
// }
//
// int cppyy_datamember_index_enumerated(cppyy_scope_t scope, int datamember_index) {
//     return (int)Cppyy::GetDatamemberIndexEnumerated(scope, datamember_index);
// }
//
//
// [> data member properties ------------------------------------------------- <]
// int cppyy_is_publicdata(cppyy_type_t type, cppyy_index_t datamember_index) {
//     return (int)Cppyy::IsPublicData(type, datamember_index);
// }
//
// int cppyy_is_protecteddata(cppyy_type_t type, cppyy_index_t datamember_index) {
//     return (int)Cppyy::IsProtectedData(type, datamember_index);
// }
//
// int cppyy_is_staticdata(cppyy_type_t type, cppyy_index_t datamember_index) {
//     return (int)Cppyy::IsStaticData(type, datamember_index);
// }
//
// int cppyy_is_const_data(cppyy_scope_t scope, cppyy_index_t idata) {
//     return (int)Cppyy::IsConstData(scope, idata);
// }
//
// int cppyy_is_enum_data(cppyy_scope_t scope, cppyy_index_t idata) {
//     return (int)Cppyy::IsEnumData(scope, idata);
// }
//
// int cppyy_get_dimension_size(cppyy_scope_t scope, cppyy_index_t idata, int dimension) {
//     return Cppyy::GetDimensionSize(scope, idata, dimension);
// }
//
//
// [> enum properties -------------------------------------------------------- <]
// cppyy_enum_t cppyy_get_enum(cppyy_scope_t scope, const char* enum_name) {
//     return Cppyy::GetEnum(scope, enum_name);
// }
//
// cppyy_index_t cppyy_get_num_enum_data(cppyy_enum_t e) {
//     return Cppyy::GetNumEnumData(e);
// }
//
// const char* cppyy_get_enum_data_name(cppyy_enum_t e, cppyy_index_t idata) {
//     return cppstring_to_cstring(Cppyy::GetEnumDataName(e, idata));
// }
//
// long long cppyy_get_enum_data_value(cppyy_enum_t e, cppyy_index_t idata) {
//     return Cppyy::GetEnumDataValue(e, idata);
// }
//
//
// [> misc helpers ----------------------------------------------------------- <]
// RPY_EXTERN
// void* cppyy_load_dictionary(const char* lib_name) {
//     int result = gSystem->Load(lib_name);
//     return (void*)(result == 0 [> success */ || result == 1 /* already loaded <]);
// }
//
// #if defined(_MSC_VER)
// long long cppyy_strtoll(const char* str) {
//     return _strtoi64(str, NULL, 0);
// }
//
// extern "C" {
// unsigned long long cppyy_strtoull(const char* str) {
//     return _strtoui64(str, NULL, 0);
// }
// }
// #else
// long long cppyy_strtoll(const char* str) {
//     return strtoll(str, NULL, 0);
// }
//
// extern "C" {
// unsigned long long cppyy_strtoull(const char* str) {
//     return strtoull(str, NULL, 0);
// }
// }
// #endif
//
// void cppyy_free(void* ptr) {
//     free(ptr);
// }
//
// cppyy_object_t cppyy_charp2stdstring(const char* str, size_t sz) {
//     return (cppyy_object_t)new std::string(str, sz);
// }
//
// const char* cppyy_stdstring2charp(cppyy_object_t ptr, size_t* lsz) {
//     *lsz = ((std::string*)ptr)->size();
//     return ((std::string*)ptr)->data();
// }
//
// cppyy_object_t cppyy_stdstring2stdstring(cppyy_object_t ptr) {
//     return (cppyy_object_t)new std::string(*(std::string*)ptr);
// }
//
// double cppyy_longdouble2double(void* p) {
//     return (double)*(long double*)p;
// }
//
// void cppyy_double2longdouble(double d, void* p) {
//     *(long double*)p = d;
// }
//
// int cppyy_vectorbool_getitem(cppyy_object_t ptr, int idx) {
//     return (int)(*(std::vector<bool>*)ptr)[idx];
// }
//
// void cppyy_vectorbool_setitem(cppyy_object_t ptr, int idx, int value) {
//     (*(std::vector<bool>*)ptr)[idx] = (bool)value;
// }

} // end C-linkage wrappers

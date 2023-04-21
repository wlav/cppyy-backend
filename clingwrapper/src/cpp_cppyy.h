#ifndef CPYCPPYY_CPPYY_H
#define CPYCPPYY_CPPYY_H

// Standard
#include <cassert>
#include <set>
#include <string>
#include <vector>
#include <stddef.h>
#include <stdint.h>
#include <iostream>

#include "callcontext.h"

// #include "TCling.h"

#include "clang/Interpreter/InterOp.h"

// some more types; assumes Cppyy.h follows Python.h
#ifndef PY_LONG_LONG
#ifdef _WIN32
typedef __int64 PY_LONG_LONG;
#else
typedef long long PY_LONG_LONG;
#endif
#endif

#ifndef PY_ULONG_LONG
#ifdef _WIN32
typedef unsigned __int64   PY_ULONG_LONG;
#else
typedef unsigned long long PY_ULONG_LONG;
#endif
#endif

#ifndef PY_LONG_DOUBLE
typedef long double PY_LONG_DOUBLE;
#endif

typedef CPyCppyy::Parameter Parameter;

// small number that allows use of stack for argument passing
const int SMALL_ARGS_N = 8;

// convention to pass flag for direct calls (similar to Python's vector calls)
#define DIRECT_CALL ((size_t)1 << (8 * sizeof(size_t) - 1))
static inline size_t CALL_NARGS(size_t nargs) {
    return nargs & ~DIRECT_CALL;
}

// namespace cling
// {
// namespace cppyy
// {
//     extern inline cling::Interpreter * gCling = 0;
// }
// }
//

namespace Cppyy {
    typedef InterOp::TCppScope_t    TCppScope_t;
    typedef InterOp::TCppType_t     TCppType_t;
    typedef InterOp::TCppScope_t    TCppEnum_t;
    typedef InterOp::TCppScope_t    TCppObject_t;
    typedef InterOp::TCppFunction_t TCppMethod_t;
    typedef InterOp::TCppIndex_t    TCppIndex_t;
    typedef intptr_t                TCppFuncAddr_t;

// // direct interpreter access -------------------------------------------------
    RPY_EXPORTED
    bool Compile(const std::string& code, bool silent = false);
    RPY_EXPORTED
    std::string ToString(TCppType_t klass, TCppObject_t obj);
//
// // name to opaque C++ scope representation -----------------------------------
    RPY_EXPORTED
    std::string ResolveName(const std::string& cppitem_name);
    RPY_EXPORTED
    TCppType_t ResolveType(TCppType_t cppitem_name);
    RPY_EXPORTED
    TCppType_t GetRealType(TCppType_t type);
    RPY_EXPORTED
    std::string ResolveEnum(TCppScope_t enum_scope);
    RPY_EXPORTED
    bool IsClassType(TCppType_t type);
    RPY_EXPORTED
    TCppType_t GetType(const std::string &name, bool enable_slow_lookup = false);
    RPY_EXPORTED
    TCppType_t GetComplexType(const std::string &element_type);
    RPY_EXPORTED
    TCppScope_t GetScope(const std::string& scope_name,
                         TCppScope_t parent_scope = 0);
    RPY_EXPORTED
    TCppScope_t GetFullScope(const std::string& scope_name);
    RPY_EXPORTED
    TCppScope_t GetTypeScope(TCppScope_t klass);
    RPY_EXPORTED
    TCppScope_t GetNamed(const std::string& scope_name,
                         TCppScope_t parent_scope = 0);
    RPY_EXPORTED
    TCppScope_t GetParentScope(TCppScope_t scope);
    RPY_EXPORTED
    TCppScope_t GetScopeFromType(TCppType_t type);
    RPY_EXPORTED
    TCppType_t  GetTypeFromScope(TCppScope_t klass);
    RPY_EXPORTED
    TCppScope_t GetGlobalScope();
    RPY_EXPORTED
    TCppType_t  GetActualClass(TCppType_t klass, TCppObject_t obj) { assert(0 && "GetActualClass"); return 0; }
    RPY_EXPORTED
    size_t      SizeOf(TCppType_t klass);
    RPY_EXPORTED
    size_t      SizeOf(const std::string &type) { assert(0 && "SizeOf"); return 0; }

    RPY_EXPORTED
    bool        IsBuiltin(const std::string& type_name);

    RPY_EXPORTED
    bool        IsBuiltin(TCppType_t type);

    RPY_EXPORTED
    bool        IsComplete(TCppScope_t type);

//     RPY_EXPORTED
//     inline TCppScope_t gGlobalScope = 0;      // for fast access
//
// // memory management ---------------------------------------------------------
    RPY_EXPORTED
    TCppObject_t Allocate(TCppType_t type) { assert(0 && "Allocate"); return 0; }
    RPY_EXPORTED
    void         Deallocate(TCppType_t type, TCppObject_t instance) { assert(0 && "Deallocate"); return; }
    RPY_EXPORTED
    TCppObject_t Construct(TCppType_t type, void* arena = nullptr) { assert(0 && "Construct"); return 0; }
    RPY_EXPORTED
    void         Destruct(TCppType_t type, TCppObject_t instance) { assert(0 && "Deconstruct"); return; }

// method/function dispatching -----------------------------------------------
    RPY_EXPORTED
    void          CallV(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    unsigned char CallB(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    char          CallC(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    short         CallH(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    int           CallI(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    long          CallL(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    PY_LONG_LONG  CallLL(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    float         CallF(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    double        CallD(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    PY_LONG_DOUBLE CallLD(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);

    RPY_EXPORTED
    void*         CallR(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args);
    RPY_EXPORTED
    char*         CallS(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args, size_t* length);
    RPY_EXPORTED
    TCppObject_t  CallConstructor(TCppMethod_t method, TCppScope_t klass, size_t nargs, void* args);
    RPY_EXPORTED
    void          CallDestructor(TCppType_t type, TCppObject_t self) { return; }
    RPY_EXPORTED
    TCppObject_t  CallO(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args, TCppType_t result_type);

    RPY_EXPORTED
    TCppFuncAddr_t GetFunctionAddress(TCppMethod_t method, bool check_enabled=true);

// // handling of function argument buffer --------------------------------------
    RPY_EXPORTED
    void*  AllocateFunctionArgs(size_t nargs);
    RPY_EXPORTED
    void   DeallocateFunctionArgs(void* args);
    RPY_EXPORTED
    size_t GetFunctionArgSizeof();
    RPY_EXPORTED
    size_t GetFunctionArgTypeoffset();

// // scope reflection information ----------------------------------------------
    RPY_EXPORTED
    bool IsNamespace(TCppScope_t scope);
    RPY_EXPORTED
    bool IsClass(TCppScope_t scope);
    RPY_EXPORTED
    bool IsTemplate(TCppScope_t scope);
    RPY_EXPORTED
    bool IsTemplateInstantiation(TCppScope_t scope);
    RPY_EXPORTED
    bool IsAbstract(TCppScope_t scope);
    RPY_EXPORTED
    bool IsEnumScope(TCppScope_t scope);
    RPY_EXPORTED
    bool IsEnumConstant(TCppScope_t scope);
    RPY_EXPORTED
    bool IsEnumType(TCppType_t type);
    RPY_EXPORTED
    bool IsAggregate(TCppType_t type);
    RPY_EXPORTED
    bool IsDefaultConstructable(TCppScope_t scope);
    RPY_EXPORTED
    bool IsVariable(TCppScope_t scope);

    RPY_EXPORTED
    void GetAllCppNames(TCppScope_t scope, std::set<std::string>& cppnames) { return; }

// // namespace reflection information ------------------------------------------
    RPY_EXPORTED
    std::vector<Cppyy::TCppScope_t> GetUsingNamespaces(TCppScope_t);
//
// // class reflection information ----------------------------------------------
    RPY_EXPORTED
    std::string GetFinalName(TCppType_t type);
    RPY_EXPORTED
    std::string GetScopedFinalName(TCppType_t type);
    RPY_EXPORTED
    bool        HasVirtualDestructor(TCppType_t type);
    RPY_EXPORTED
    bool        HasComplexHierarchy(TCppType_t type) { assert(0 && "HasComplexHierarchy"); return false; }
    RPY_EXPORTED
    TCppIndex_t GetNumBases(TCppScope_t klass);
    RPY_EXPORTED
    std::string GetBaseName(TCppScope_t klass, TCppIndex_t ibase);
    RPY_EXPORTED
    TCppScope_t GetBaseScope(TCppScope_t klass, TCppIndex_t ibase);
    RPY_EXPORTED
    bool        IsSubclass(TCppType_t derived, TCppType_t base);
    RPY_EXPORTED
    bool        IsSmartPtr(TCppType_t type)  { return false; }
    RPY_EXPORTED
    bool        GetSmartPtrInfo(const std::string&, TCppType_t* raw, TCppMethod_t* deref) { assert(0 && "GetSmartPtrInfo"); return false; }
    RPY_EXPORTED
    void        AddSmartPtrType(const std::string&) { assert(0 && "AddSmartPtrType"); return; }

    RPY_EXPORTED
    void        AddTypeReducer(const std::string& reducable, const std::string& reduced) { assert(0 && "AddTypeReducer"); return; }

// // calculate offsets between declared and actual type, up-cast: direction > 0; down-cast: direction < 0
    RPY_EXPORTED
    ptrdiff_t GetBaseOffset(
        TCppType_t derived, TCppType_t base, TCppObject_t address, int direction, bool rerror = false);

// // method/function reflection information ------------------------------------
    RPY_EXPORTED
    std::vector<TCppMethod_t> GetClassMethods(TCppScope_t scope);
    RPY_EXPORTED
    std::vector<TCppScope_t> GetMethodsFromName(TCppScope_t scope,
                                                const std::string& name);

    RPY_EXPORTED
    TCppMethod_t GetMethod(TCppScope_t scope, TCppIndex_t imeth) { return 0; }

    RPY_EXPORTED
    std::string GetMethodName(TCppMethod_t);
    RPY_EXPORTED
    std::string GetMethodFullName(TCppMethod_t);
    // GetMethodMangledName is unused.
    RPY_EXPORTED
    std::string GetMethodMangledName(TCppMethod_t) { assert(0 && "GetMethodMangledName"); return ""; }
    RPY_EXPORTED
    TCppType_t GetMethodReturnType(TCppMethod_t);
    RPY_EXPORTED
    std::string GetMethodReturnTypeAsString(TCppMethod_t);
    RPY_EXPORTED
    TCppIndex_t GetMethodNumArgs(TCppMethod_t);
    RPY_EXPORTED
    TCppIndex_t GetMethodReqArgs(TCppMethod_t);
    RPY_EXPORTED
    std::string GetMethodArgName(TCppMethod_t, TCppIndex_t iarg);
    RPY_EXPORTED
    TCppType_t GetMethodArgType(TCppMethod_t, TCppIndex_t iarg);
    RPY_EXPORTED
    std::string GetMethodArgTypeAsString(TCppMethod_t method, TCppIndex_t iarg);
    RPY_EXPORTED
    std::string GetMethodArgDefault(TCppMethod_t, TCppIndex_t iarg);
    RPY_EXPORTED
    std::string GetMethodSignature(TCppMethod_t, bool show_formal_args, TCppIndex_t max_args = (TCppIndex_t)-1);
    // GetMethodPrototype is unused.
    RPY_EXPORTED
    std::string GetMethodPrototype(TCppMethod_t, bool show_formal_args);
    RPY_EXPORTED
    bool        IsConstMethod(TCppMethod_t);

    RPY_EXPORTED
    TCppIndex_t GetNumTemplatedMethods(TCppScope_t scope, bool accept_namespace = false) { assert(0 && "GetNumTemplatedMethods"); return 0; }
    RPY_EXPORTED
    std::string GetTemplatedMethodName(TCppScope_t scope, TCppIndex_t imeth) { assert(0 && "GetTemplatedMethodName"); return 0; }
    RPY_EXPORTED
    bool        IsTemplatedConstructor(TCppScope_t scope, TCppIndex_t imeth) { assert(0 && "IsTemplatedConstructor"); return 0; }
    RPY_EXPORTED
    bool        ExistsMethodTemplate(TCppScope_t scope, const std::string& name);
    RPY_EXPORTED
    bool        IsTemplatedMethod(TCppMethod_t method);
    RPY_EXPORTED
    TCppMethod_t GetMethodTemplate(
        TCppScope_t scope, const std::string& name, const std::string& proto);

    RPY_EXPORTED
    TCppIndex_t  GetGlobalOperator(
        TCppType_t scope, const std::string& lc, const std::string& rc, const std::string& op) { assert(0 && "GetGlobalOperator");return 0; }

// method properties ---------------------------------------------------------
    RPY_EXPORTED
    bool IsPublicMethod(TCppMethod_t method);
    RPY_EXPORTED
    bool IsProtectedMethod(TCppMethod_t method);
    RPY_EXPORTED
    bool IsPrivateMethod(TCppMethod_t method);
    RPY_EXPORTED
    bool IsConstructor(TCppMethod_t method);
    RPY_EXPORTED
    bool IsDestructor(TCppMethod_t method);
    RPY_EXPORTED
    bool IsStaticMethod(TCppMethod_t method);

// // data member reflection information ----------------------------------------
    // GetNumDatamembers is unused.
    // RPY_EXPORTED
    // TCppIndex_t GetNumDatamembers(TCppScope_t scope, bool accept_namespace = false) { return 0; }
    RPY_EXPORTED
    std::vector<TCppScope_t> GetDatamembers(TCppScope_t scope);
    // GetDatamemberName is unused.
    // RPY_EXPORTED
    // std::string GetDatamemberName(TCppScope_t scope, TCppIndex_t idata) { return ""; }
    RPY_EXPORTED
    TCppType_t GetDatamemberType(TCppScope_t data);
    RPY_EXPORTED
    std::string GetDatamemberTypeAsString(TCppScope_t var);
    RPY_EXPORTED
    std::string GetTypeAsString(TCppType_t type);
    RPY_EXPORTED
    intptr_t    GetDatamemberOffset(TCppScope_t var);
    RPY_EXPORTED
    bool CheckDatamember(TCppScope_t scope, const std::string& name);

// // data member properties ----------------------------------------------------
    RPY_EXPORTED
    bool IsPublicData(TCppScope_t var);
    RPY_EXPORTED
    bool IsProtectedData(TCppScope_t var);
    RPY_EXPORTED
    bool IsPrivateData(TCppScope_t var);
    RPY_EXPORTED
    bool IsStaticDatamember(TCppScope_t var);
    RPY_EXPORTED
    bool IsConstVar(TCppScope_t var);
//     RPY_EXPORTED
//     bool IsEnumData(TCppScope_t scope, TCppIndex_t idata);
    RPY_EXPORTED
    std::vector<long int> GetDimensions(TCppType_t type);

// // enum properties -----------------------------------------------------------
    // GetEnum is unused.
    // RPY_EXPORTED
    // TCppEnum_t  GetEnum(TCppScope_t scope, const std::string& enum_name) { return 0; }
    RPY_EXPORTED
    std::vector<TCppScope_t> GetEnumConstants(TCppScope_t scope);
    // GetEnumDataName is unused.
    // RPY_EXPORTED
    // std::string GetEnumDataName(TCppEnum_t, TCppIndex_t idata) { return ""; }
    RPY_EXPORTED
    TCppType_t  GetEnumConstantType(TCppScope_t scope);
    RPY_EXPORTED
    TCppIndex_t GetEnumDataValue(TCppScope_t scope);

    RPY_EXPORTED
    TCppScope_t InstantiateTemplateClass(
            TCppScope_t tmpl, InterOp::TemplateArgInfo* args, size_t args_size);

    RPY_EXPORTED
    void        DumpScope(TCppScope_t scope);
} // namespace Cppyy


// class CallWrapper {
// public:
//     typedef const void* DeclId_t;
//
// public:
//     CallWrapper(void * f) : fDecl(nullptr), fName(""), fTF(nullptr) { assert(0); }
//     CallWrapper(DeclId_t fid, const std::string& n) : fDecl(fid), fName(n), fTF(nullptr) {}
//     ~CallWrapper() {}
//
// public:
//     cling::Interpreter::CallFuncIFacePtr_t fFaceptr;
//     DeclId_t      fDecl;
//     std::string   fName;
//     void *    fTF;
// };
//
//
//
// inline std::vector<CallWrapper*> gWrapperHolder;
//
// inline
// CallWrapper* new_CallWrapper(CallWrapper::DeclId_t fid, const std::string& n);
//
// inline
// bool WrapperCall(Cppyy::TCppMethod_t method, size_t nargs, void* args_, void* self, void* result);

#endif // !CPYCPPYY_CPPYY_H

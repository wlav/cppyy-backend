// @(#)root/core:$Id$

#ifndef R__TSCHEMATYPE_H
#define R__TSCHEMATYPE_H

// NOTE: #included by libCoreLegacy and libCling. All symbols must be inline.

#include <string>
#include <map>

namespace CppyyLegacy {
namespace Internal {
   struct TSchemaType {
      TSchemaType() = default;
      TSchemaType(const char *type, const char *dim) : fType(type),fDimensions(dim) {}
      TSchemaType(const std::string &type, const std::string &dim) : fType(type),fDimensions(dim) {}
      std::string fType;
      std::string fDimensions;
   };
   typedef std::map<std::string, std::string> MembersMap_t;
}
} // namespace CppyyLegacy

#endif // R__TSCHEMATYPE_H

// Minimal ECMA-335 metadata reader for the game's managed assemblies (Managed/*.dll): the type
// definitions, their fields with signatures, base types, nesting, generic parameters and custom
// attribute types. Enough to lay out Unity's serialization of MonoBehaviours (mono_types.*).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "install/unity_file.h"

namespace dd::install::dotnet {

// A type in a signature: element type code, plus the referenced type, generic arguments or
// element type depending on the code.
struct TypeSig {
    uint8_t code = 0;               // ECMA-335 ELEMENT_TYPE_* (0x11 VALUETYPE, 0x12 CLASS, 0x15 GENERICINST...)
    uint32_t typeToken = 0;         // VALUETYPE / CLASS / GENERICINST: TypeDefOrRef as a token (table << 24 | row)
    uint32_t number = 0;            // VAR / MVAR: generic parameter index
    std::vector<TypeSig> args;      // GENERICINST: generic arguments; SZARRAY: the element type
    bool valueType = false;         // GENERICINST over a value type
};

constexpr uint8_t kVoid = 0x01, kBool = 0x02, kChar = 0x03, kI1 = 0x04, kU1 = 0x05, kI2 = 0x06, kU2 = 0x07, kI4 = 0x08,
                  kU4 = 0x09, kI8 = 0x0a, kU8 = 0x0b, kR4 = 0x0c, kR8 = 0x0d, kString = 0x0e, kValueType = 0x11, kClass = 0x12,
                  kVar = 0x13, kArray = 0x14, kGenericInst = 0x15, kI = 0x18, kU = 0x19, kObject = 0x1c, kSzArray = 0x1d,
                  kMVar = 0x1e;
constexpr uint32_t kTypeDef = 0x02, kTypeRef = 0x01, kTypeSpec = 0x1b;

struct Field {
    uint16_t flags = 0;
    std::string name;
    TypeSig type;
    std::vector<std::string> attributes; // full names of the custom attribute types
};

struct TypeDef {
    uint32_t flags = 0;
    std::string name, ns;
    uint32_t extends = 0; // token (TypeDef / TypeRef / TypeSpec), 0 for none
    int enclosing = -1;   // nested types: index of the enclosing TypeDef
    std::vector<Field> fields;
    std::vector<std::string> attributes;
    int genericParams = 0;
    std::string FullName(const std::vector<TypeDef>& all) const; // Outer/Inner for nested types
};

struct TypeRef {
    std::string name, ns;
    uint32_t scope = 0; // ResolutionScope as a token: 0x23 AssemblyRef, 0x01 TypeRef (nesting), 0x00 Module
};

class Assembly {
public:
    bool Load(const Bytes& pe, std::string* err);
    std::string name;
    std::vector<TypeDef> types;
    std::vector<TypeRef> typeRefs;
    std::vector<TypeSig> typeSpecs;
    std::vector<std::string> assemblyRefs;
    // TypeDef index by full name ("Namespace.Name", nested "Namespace.Outer/Inner")
    int Find(const std::string& fullName) const;

private:
    std::unordered_map<std::string, int> byName_;
};

// ECMA-335 field and type attribute bits used by the layout rules
constexpr uint16_t kFieldAccessMask = 0x0007, kFieldPublic = 0x0006, kFieldStatic = 0x0010, kFieldInitOnly = 0x0020,
                   kFieldLiteral = 0x0040, kFieldNotSerialized = 0x0080;
constexpr uint32_t kTypeInterface = 0x20, kTypeAbstract = 0x80, kTypeSerializable = 0x2000;

} // namespace dd::install::dotnet

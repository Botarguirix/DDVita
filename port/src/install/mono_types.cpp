#include "install/mono_types.h"

#include <cstdio>

#include "install/game_files.h"

namespace dd::install {

using namespace dotnet;

struct ScriptLayouts::Ty {
    const Assembly* assembly = nullptr;
    TypeSig sig;
    std::vector<Ty> ctx; // the enclosing type's generic arguments (for VAR)
};

namespace {

// Unity's serialization depth limit: classes nested deeper than this are not serialized
constexpr int kMaxDepth = 7;

using Ty = ScriptLayouts::Ty;

Ty Deref(Ty t) {
    for (int guard = 0; t.sig.code == kVar && t.sig.number < t.ctx.size() && guard < 16; guard++) {
        Ty next = t.ctx[t.sig.number];
        t = std::move(next);
    }
    return t;
}

void Push(TypeTree* out, int level, const std::string& type, const std::string& name, uint32_t flag = 0) {
    out->nodes.push_back({level, type, name, 0, flag});
}

void PushString(TypeTree* out, int level, const std::string& name) {
    Push(out, level, "string", name);
    Push(out, level + 1, "Array", "Array", 0x4000);
    Push(out, level + 2, "int", "size");
    Push(out, level + 2, "char", "data");
}

void PushPPtr(TypeTree* out, int level, const std::string& type, const std::string& name) {
    Push(out, level, "PPtr<$" + type + ">", name);
    Push(out, level + 1, "int", "m_FileID");
    Push(out, level + 1, "SInt64", "m_PathID");
}

void PushFloats(TypeTree* out, int level, const char* type, const std::string& name, std::initializer_list<const char*> fields,
                const char* fieldType = "float") {
    Push(out, level, type, name);
    for (const char* f : fields) Push(out, level + 1, fieldType, f);
}

// primitive element types: (type name, aligned)
bool Primitive(uint8_t code, const char** type, bool* align) {
    *align = false;
    switch (code) {
    case kBool: *type = "UInt8"; *align = true; return true;
    case kU1: *type = "UInt8"; *align = true; return true;
    case kI1: *type = "SInt8"; *align = true; return true;
    case kChar: *type = "UInt16"; *align = true; return true;
    case kI2: *type = "SInt16"; *align = true; return true;
    case kU2: *type = "UInt16"; *align = true; return true;
    case kI4: *type = "int"; return true;
    case kU4: *type = "unsigned int"; return true;
    case kI8: *type = "SInt64"; return true;
    case kU8: *type = "UInt64"; return true;
    case kR4: *type = "float"; return true;
    case kR8: *type = "double"; return true;
    default: return false;
    }
}

bool BaseStopsHere(const std::string& n) {
    return n == "UnityEngine.MonoBehaviour" || n == "UnityEngine.ScriptableObject" || n == "UnityEngine.Object" ||
           n == "UnityEngine.Behaviour" || n == "UnityEngine.Component" || n == "System.Object" || n == "System.ValueType";
}

bool HasAttr(const std::vector<std::string>& attrs, const char* name) {
    for (const std::string& a : attrs)
        if (a == name) return true;
    return false;
}

} // namespace

const Assembly* ScriptLayouts::Load(std::string name) {
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".dll") == 0) name.resize(name.size() - 4);
    auto it = assemblies_.find(name);
    if (it != assemblies_.end()) return it->second.get();
    auto a = std::make_unique<Assembly>();
    Bytes pe;
    std::string err;
    if (!ReadFile(dir_ + "/" + name + ".dll", &pe) || !a->Load(pe, &err)) a.reset();
    return (assemblies_[name] = std::move(a)).get();
}

ScriptLayouts::Resolved ScriptLayouts::FindAnywhere(const std::string& fullName) {
    // forwarded types (netstandard -> mscorlib) and the like: try the usual homes
    for (const char* n : {"mscorlib", "UnityEngine.CoreModule", "System", "System.Core"}) {
        const Assembly* a = Load(n);
        if (a) {
            int i = a->Find(fullName);
            if (i >= 0) return {a, i};
        }
    }
    for (auto& [name, a] : assemblies_) {
        if (!a) continue;
        int i = a->Find(fullName);
        if (i >= 0) return {a.get(), i};
    }
    return {};
}

ScriptLayouts::Resolved ScriptLayouts::Resolve(const Assembly& in, uint32_t token, int depth) {
    uint32_t table = token >> 24, row = token & 0xffffff;
    if (depth > 8 || row == 0) return {};
    if (table == kTypeDef) return row <= in.types.size() ? Resolved{&in, static_cast<int>(row - 1)} : Resolved{};
    if (table == kTypeSpec) {
        if (row > in.typeSpecs.size()) return {};
        const TypeSig& s = in.typeSpecs[row - 1];
        return s.typeToken ? Resolve(in, s.typeToken, depth + 1) : Resolved{};
    }
    if (table != kTypeRef || row > in.typeRefs.size()) return {};
    const TypeRef& r = in.typeRefs[row - 1];
    std::string name = r.ns.empty() ? r.name : r.ns + "." + r.name;
    uint32_t scope = r.scope >> 24, srow = r.scope & 0xffffff;
    if (scope == kTypeRef) { // nested type: Outer/Inner
        Resolved outer = Resolve(in, r.scope, depth + 1);
        if (!outer) return {};
        int i = outer.assembly->Find(outer.FullName() + "/" + r.name);
        return i >= 0 ? Resolved{outer.assembly, i} : Resolved{};
    }
    if (scope == 0x23 && srow >= 1 && srow <= in.assemblyRefs.size()) {
        const Assembly* a = Load(in.assemblyRefs[srow - 1]);
        if (a) {
            int i = a->Find(name);
            if (i >= 0) return {a, i};
        }
        return FindAnywhere(name);
    }
    int i = in.Find(name);
    return i >= 0 ? Resolved{&in, i} : FindAnywhere(name);
}

bool ScriptLayouts::DerivesFrom(Resolved t, const char* fullName) {
    for (int guard = 0; t && guard < 32; guard++) {
        if (t.FullName() == fullName) return true;
        if (!t.def().extends) return false;
        t = Resolve(*t.assembly, t.def().extends);
    }
    return false;
}

// Native Unity types with a fixed serialized layout (as Unity 2020.3 writes them)
bool ScriptLayouts::AddSpecial(const std::string& full, const std::string& name, int level, TypeTree* out) {
    if (full == "UnityEngine.Vector2") return PushFloats(out, level, "Vector2", name, {"x", "y"}), true;
    if (full == "UnityEngine.Vector3") return PushFloats(out, level, "Vector3", name, {"x", "y", "z"}), true;
    if (full == "UnityEngine.Vector4") return PushFloats(out, level, "Vector4", name, {"x", "y", "z", "w"}), true;
    if (full == "UnityEngine.Quaternion") return PushFloats(out, level, "Quaternion", name, {"x", "y", "z", "w"}), true;
    if (full == "UnityEngine.Color") return PushFloats(out, level, "Color", name, {"r", "g", "b", "a"}), true;
    if (full == "UnityEngine.Rect") return PushFloats(out, level, "Rect", name, {"x", "y", "width", "height"}), true;
    if (full == "UnityEngine.Vector2Int") return PushFloats(out, level, "Vector2Int", name, {"x", "y"}, "int"), true;
    if (full == "UnityEngine.Vector3Int") return PushFloats(out, level, "Vector3Int", name, {"x", "y", "z"}, "int"), true;
    if (full == "UnityEngine.Color32") return PushFloats(out, level, "Color32", name, {"rgba"}, "unsigned int"), true;
    if (full == "UnityEngine.LayerMask") return PushFloats(out, level, "LayerMask", name, {"m_Bits"}, "unsigned int"), true;
    if (full == "UnityEngine.RectOffset")
        return PushFloats(out, level, "RectOffset", name, {"m_Left", "m_Right", "m_Top", "m_Bottom"}, "int"), true;
    if (full == "UnityEngine.PropertyName") {
        Push(out, level, "PropertyName", name);
        PushString(out, level + 1, "id");
        return true;
    }
    if (full == "UnityEngine.Bounds") {
        Push(out, level, "Bounds", name);
        PushFloats(out, level + 1, "Vector3", "m_Center", {"x", "y", "z"});
        PushFloats(out, level + 1, "Vector3", "m_Extent", {"x", "y", "z"});
        return true;
    }
    if (full == "UnityEngine.AnimationCurve") {
        Push(out, level, "AnimationCurve", name);
        Push(out, level + 1, "vector", "m_Curve");
        Push(out, level + 2, "Array", "Array", 0x4000);
        Push(out, level + 3, "int", "size");
        Push(out, level + 3, "Keyframe", "data");
        for (const char* f : {"time", "value", "inSlope", "outSlope"}) Push(out, level + 4, "float", f);
        Push(out, level + 4, "int", "weightedMode");
        Push(out, level + 4, "float", "inWeight");
        Push(out, level + 4, "float", "outWeight");
        for (const char* f : {"m_PreInfinity", "m_PostInfinity", "m_RotationOrder"}) Push(out, level + 1, "int", f);
        return true;
    }
    if (full == "UnityEngine.Gradient") {
        Push(out, level, "Gradient", name);
        for (int i = 0; i < 8; i++) PushFloats(out, level + 1, "ColorRGBA", "key" + std::to_string(i), {"r", "g", "b", "a"});
        for (int i = 0; i < 8; i++) Push(out, level + 1, "UInt16", "ctime" + std::to_string(i));
        for (int i = 0; i < 8; i++) Push(out, level + 1, "UInt16", "atime" + std::to_string(i));
        Push(out, level + 1, "int", "m_Mode");
        Push(out, level + 1, "UInt8", "m_NumColorKeys");
        Push(out, level + 1, "UInt8", "m_NumAlphaKeys", 0x4000);
        return true;
    }
    if (full == "UnityEngine.GUIStyle") {
        Push(out, level, "GUIStyle", name);
        PushString(out, level + 1, "m_Name");
        for (const char* s : {"m_Normal", "m_Hover", "m_Active", "m_Focused", "m_OnNormal", "m_OnHover", "m_OnActive", "m_OnFocused"}) {
            Push(out, level + 1, "GUIStyleState", s);
            Push(out, level + 2, "PPtr<Texture2D>", "m_Background");
            Push(out, level + 3, "int", "m_FileID");
            Push(out, level + 3, "SInt64", "m_PathID");
            PushFloats(out, level + 2, "ColorRGBA", "m_TextColor", {"r", "g", "b", "a"});
        }
        for (const char* s : {"m_Border", "m_Margin", "m_Padding", "m_Overflow"})
            PushFloats(out, level + 1, "RectOffset", s, {"m_Left", "m_Right", "m_Top", "m_Bottom"}, "int");
        Push(out, level + 1, "PPtr<Font>", "m_Font");
        Push(out, level + 2, "int", "m_FileID");
        Push(out, level + 2, "SInt64", "m_PathID");
        for (const char* s : {"m_FontSize", "m_FontStyle", "m_Alignment"}) Push(out, level + 1, "int", s);
        Push(out, level + 1, "bool", "m_WordWrap");
        Push(out, level + 1, "bool", "m_RichText", 0x4000);
        Push(out, level + 1, "int", "m_TextClipping");
        Push(out, level + 1, "int", "m_ImagePosition");
        PushFloats(out, level + 1, "Vector2f", "m_ContentOffset", {"x", "y"});
        Push(out, level + 1, "float", "m_FixedWidth");
        Push(out, level + 1, "float", "m_FixedHeight");
        Push(out, level + 1, "bool", "m_StretchWidth");
        Push(out, level + 1, "bool", "m_StretchHeight", 0x4000);
        return true;
    }
    return false;
}

bool ScriptLayouts::AddField(const std::string& name, const Ty& type, bool reference, int level, int depth, TypeTree* out) {
    Ty t = Deref(type);
    const TypeSig& s = t.sig;
    bool isList = false;
    if (s.code == kGenericInst && s.args.size() == 1) {
        Resolved g = Resolve(*t.assembly, s.typeToken);
        isList = g && g.FullName() == "System.Collections.Generic.List`1";
    }
    if (reference && s.code != kSzArray && !isList) { // [SerializeReference]: an id into the references registry
        Push(out, level, "managedReference", name);
        Push(out, level + 1, "int", "id");
        return true;
    }
    const char* prim;
    bool align;
    if (Primitive(s.code, &prim, &align)) {
        Push(out, level, prim, name, align ? 0x4000 : 0);
        return true;
    }
    if (s.code == kString) {
        PushString(out, level, name);
        return true;
    }

    // arrays and List<T>
    const TypeSig* elemSig = nullptr;
    if ((s.code == kSzArray || isList) && !s.args.empty()) elemSig = &s.args[0];
    if (elemSig) {
        Ty elem = Deref(Ty{t.assembly, *elemSig, t.ctx});
        if (elem.sig.code == kSzArray) return false; // nested arrays are not serialized
        if (elem.sig.code == kGenericInst) {
            Resolved g = Resolve(*elem.assembly, elem.sig.typeToken);
            if (g && g.FullName() == "System.Collections.Generic.List`1") return false;
        }
        size_t mark = out->nodes.size();
        Push(out, level, reference ? "managedReference" : "vector", name);
        Push(out, level + 1, "Array", "Array", 0x4000);
        Push(out, level + 2, "int", "size");
        if (!AddField("data", elem, reference, level + 2, depth, out)) {
            out->nodes.resize(mark);
            return false;
        }
        return true;
    }

    if (s.code != kValueType && s.code != kClass && s.code != kGenericInst) return false;
    Resolved r = Resolve(*t.assembly, s.typeToken);
    if (!r) return false;
    std::string full = r.FullName();
    const TypeDef& d = r.def();
    // enums: their underlying type
    if (d.extends && DerivesFrom(Resolve(*r.assembly, d.extends), "System.Enum")) {
        for (const Field& f : d.fields)
            if (!(f.flags & kFieldStatic) && Primitive(f.type.code, &prim, &align)) {
                Push(out, level, prim, name, align ? 0x4000 : 0);
                return true;
            }
        return false;
    }
    if (AddSpecial(full, name, level, out)) return true;
    if (DerivesFrom(r, "UnityEngine.Object")) {
        PushPPtr(out, level, d.name, name);
        return true;
    }
    if (!(d.flags & kTypeSerializable) || (d.flags & (kTypeInterface | kTypeAbstract))) return false;
    if (full.rfind("System.", 0) == 0) return false;
    if (depth >= kMaxDepth) return false;
    std::vector<Ty> args;
    if (s.code == kGenericInst)
        for (const TypeSig& a : s.args) args.push_back(Ty{t.assembly, a, t.ctx});
    Push(out, level, d.name, name);
    AddFields(r, args, level + 1, depth + 1, out);
    return true;
}

void ScriptLayouts::AddFields(Resolved t, const std::vector<Ty>& args, int level, int depth, TypeTree* out) {
    const TypeDef& d = t.def();
    if (d.extends) {
        Ty base;
        base.assembly = t.assembly;
        base.ctx = args;
        if (d.extends >> 24 == kTypeSpec && (d.extends & 0xffffff) <= t.assembly->typeSpecs.size())
            base.sig = t.assembly->typeSpecs[(d.extends & 0xffffff) - 1];
        else
            base.sig.code = kClass, base.sig.typeToken = d.extends;
        Resolved b = Resolve(*t.assembly, d.extends);
        if (b && !BaseStopsHere(b.FullName())) {
            std::vector<Ty> baseArgs;
            if (base.sig.code == kGenericInst)
                for (const TypeSig& a : base.sig.args) baseArgs.push_back(Ty{t.assembly, a, args});
            AddFields(b, baseArgs, level, depth, out);
        }
    }
    for (const Field& f : d.fields) {
        if (f.flags & (kFieldStatic | kFieldLiteral | kFieldNotSerialized | kFieldInitOnly)) continue;
        bool reference = HasAttr(f.attributes, "UnityEngine.SerializeReference");
        if ((f.flags & kFieldAccessMask) != kFieldPublic && !reference && !HasAttr(f.attributes, "UnityEngine.SerializeField")) continue;
        if (f.type.code == 0) continue;
        AddField(f.name, Ty{t.assembly, f.type, args}, reference, level, depth, out);
    }
}

const TypeTree* ScriptLayouts::Tree(const std::string& assembly, const std::string& fullName) {
    std::string key = assembly + "|" + fullName;
    auto it = trees_.find(key);
    if (it != trees_.end()) return it->second.get();
    std::unique_ptr<TypeTree> tree;
    const Assembly* a = Load(assembly);
    int i = a ? a->Find(fullName) : -1;
    if (i >= 0) {
        tree = std::make_unique<TypeTree>();
        // the MonoBehaviour header (m_GameObject, m_Enabled, m_Script, m_Name) as the engine writes it
        const TypeTree* base = EngineType(114);
        tree->nodes = base->nodes;
        tree->nodes[0].type = a->types[static_cast<size_t>(i)].name;
        AddFields({a, i}, {}, 1, 0, tree.get());
        tree->Link();
    }
    return (trees_[key] = std::move(tree)).get();
}

} // namespace dd::install

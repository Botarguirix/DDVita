// Type trees of the game's MonoBehaviours and ScriptableObjects, laid out from its own managed
// assemblies with Unity 2020.3's serialization rules (what UnityPy's TypeTreeGenerator does on PC):
// the serialized fields of the class and its bases, in order, recursing into [Serializable]
// types, arrays and List<T>, with PPtrs for UnityEngine.Object references.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "install/dotnet.h"
#include "install/unity_file.h"

namespace dd::install {

class ScriptLayouts {
public:
    explicit ScriptLayouts(std::string managedDir) : dir_(std::move(managedDir)) {}
    // Tree for the class (assembly "Foo.dll" or "Foo", full name "Namespace.Class"), cached;
    // nullptr if the assembly or class is not found.
    const TypeTree* Tree(const std::string& assembly, const std::string& fullName);

    struct Ty; // a signature type with the assembly its tokens belong to

private:
    struct Resolved {
        const dotnet::Assembly* assembly = nullptr;
        int index = -1;
        explicit operator bool() const { return assembly != nullptr; }
        const dotnet::TypeDef& def() const { return assembly->types[static_cast<size_t>(index)]; }
        std::string FullName() const { return def().FullName(assembly->types); }
    };

    const dotnet::Assembly* Load(std::string name);
    Resolved Resolve(const dotnet::Assembly& in, uint32_t token, int depth = 0);
    Resolved FindAnywhere(const std::string& fullName);
    bool DerivesFrom(Resolved t, const char* fullName);
    void AddFields(Resolved t, const std::vector<Ty>& genericArgs, int level, int depth, TypeTree* out);
    bool AddField(const std::string& name, const Ty& type, bool reference, int level, int depth, TypeTree* out);
    bool AddSpecial(const std::string& fullName, const std::string& name, int level, TypeTree* out);

    std::string dir_;
    std::map<std::string, std::unique_ptr<dotnet::Assembly>> assemblies_; // by name without .dll
    std::map<std::string, std::unique_ptr<TypeTree>> trees_;
};

} // namespace dd::install

// Localised text and smart-string formatting.
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "ui/ui.h"

namespace dd::ui {

namespace {

// "{1.name}": the index decides (the tables reuse names, e.g. two "{0.value}" / "{1.value}"), else the name
const std::string* Find(const Args& args, const std::string& name, int index) {
    if (index >= 0 && index < static_cast<int>(args.size())) return &args[static_cast<size_t>(index)].second;
    for (const auto& [k, v] : args)
        if (k == name) return &v;
    return nullptr;
}

} // namespace

// Unity Localization smart strings, as far as the game's tables use them: "{0}", "{name}",
// "{0.name}" and the plural formatter "{0:plural:one|other}" / "{name:plural:...}".
std::string Format(const std::string& pattern, const Args& args) {
    std::string out;
    for (size_t i = 0; i < pattern.size();) {
        char c = pattern[i];
        if (c != '{') {
            out += c;
            i++;
            continue;
        }
        size_t end = pattern.find('}', i);
        if (end == std::string::npos) {
            out += pattern.substr(i);
            break;
        }
        // nested braces of the plural forms: find the matching close
        int depth = 0;
        for (end = i; end < pattern.size(); end++) {
            if (pattern[end] == '{') depth++;
            else if (pattern[end] == '}' && --depth == 0) break;
        }
        std::string body = pattern.substr(i + 1, end - i - 1);
        i = end + 1;
        std::string key = body, fmt;
        if (size_t colon = body.find(':'); colon != std::string::npos) key = body.substr(0, colon), fmt = body.substr(colon + 1);
        std::string name = key;
        int index = -1;
        if (size_t dot = key.find('.'); dot != std::string::npos) {
            index = std::atoi(key.substr(0, dot).c_str());
            name = key.substr(dot + 1);
        } else if (!key.empty() && key[0] >= '0' && key[0] <= '9') {
            index = std::atoi(key.c_str());
            name.clear();
        }
        const std::string* v = Find(args, name, index);
        if (!v) {
            out += "{" + body + "}";
            continue;
        }
        if (fmt.rfind("plural:", 0) == 0 || fmt.rfind("p:", 0) == 0) {
            std::string forms = fmt.substr(fmt.find(':') + 1);
            std::vector<std::string> f;
            size_t a = 0;
            for (size_t b; (b = forms.find('|', a)) != std::string::npos; a = b + 1) f.push_back(forms.substr(a, b - a));
            f.push_back(forms.substr(a));
            int n = std::atoi(v->c_str());
            std::string form = n == 1 || f.size() == 1 ? f[0] : f[1];
            out += Format(form, args); // forms may hold "{}" (the value itself)
            if (size_t at = out.find("{}"); at != std::string::npos) out.replace(at, 2, *v);
            continue;
        }
        if (fmt.rfind("cond:", 0) == 0) { // "cond:>0?yes|no" (only the comparisons the tables use)
            std::string c = fmt.substr(5);
            size_t q = c.find('?');
            std::string test = c.substr(0, q), forms = q == std::string::npos ? "" : c.substr(q + 1);
            size_t bar = forms.find('|');
            std::string yes = forms.substr(0, bar), no = bar == std::string::npos ? "" : forms.substr(bar + 1);
            int n = std::atoi(v->c_str());
            bool ok = false;
            if (test.rfind(">=", 0) == 0) ok = n >= std::atoi(test.c_str() + 2);
            else if (test.rfind("<=", 0) == 0) ok = n <= std::atoi(test.c_str() + 2);
            else if (test[0] == '>') ok = n > std::atoi(test.c_str() + 1);
            else if (test[0] == '<') ok = n < std::atoi(test.c_str() + 1);
            else if (test[0] == '=') ok = n == std::atoi(test.c_str() + 1);
            out += ok ? yes : no;
            continue;
        }
        out += *v;
    }
    return out;
}

std::string StripTags(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '<') {
            size_t e = s.find('>', i);
            std::string tag = e == std::string::npos ? "" : s.substr(i + 1, e - i - 1);
            if (!tag.empty() && (tag.rfind("color", 0) == 0 || tag.rfind("/color", 0) == 0 || tag == "b" || tag == "/b" ||
                                 tag == "i" || tag == "/i" || tag.rfind("size", 0) == 0 || tag.rfind("/size", 0) == 0)) {
                i = e;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

bool Loc::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return false;
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it->is_string()) table_[it.key()] = it->get<std::string>();
    return true;
}

const std::string& Loc::Get(const std::string& key, const std::string& fallback) const {
    auto it = table_.find(key);
    return it == table_.end() ? fallback : it->second;
}

uint32_t ParseColor(const std::string& hex, float alpha) {
    if (hex.size() != 7 || hex[0] != '#') return gfx::PackColor(1, 1, 1, alpha);
    unsigned v = static_cast<unsigned>(std::strtoul(hex.c_str() + 1, nullptr, 16));
    return gfx::PackColor(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, alpha);
}

} // namespace dd::ui

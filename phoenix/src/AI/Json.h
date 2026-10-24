#pragma once
// Minimal JSON writing for the bridge protocol (numbers, bools, strings, arrays).
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Phoenix{

    inline void AppendNumber(std::string& out, float v){
        if (!std::isfinite(v)) { v = 0.0f; }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        out += buf;
    }

    inline void AppendString(std::string& out, const std::string& s){
        out += '"';
        for (char c : s){
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if ((unsigned char)c < 0x20) { out += ' '; }
            else { out += c; }
        }
        out += '"';
    }

    inline void AppendArray(std::string& out, const std::vector<float>& values){
        out += '[';
        for (size_t i = 0; i < values.size(); i++){
            if (i) { out += ','; }
            AppendNumber(out, values[i]);
        }
        out += ']';
    }

    inline void AppendVec3(std::string& out, const glm::vec3& v){
        out += '[';
        AppendNumber(out, v.x); out += ',';
        AppendNumber(out, v.y); out += ',';
        AppendNumber(out, v.z);
        out += ']';
    }

    inline std::string ErrorJson(const std::string& message){
        std::string out = "{\"ok\":false,\"error\":";
        AppendString(out, message);
        return out + "}";
    }
}

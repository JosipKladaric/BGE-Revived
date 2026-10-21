// Minimal JSON value/parser used by the BGE scene importer.
// The format is deliberately small and dependency-free; the Blender exporter is
// the only producer, so the parser can stay compact and predictable.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bge {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;

    static bool parse(std::string_view text, Json& out, std::string& error);

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double as_number(double fallback = 0.0) const { return type_ == Type::Number ? number_ : fallback; }
    float as_float(float fallback = 0.0f) const { return static_cast<float>(as_number(fallback)); }
    int as_int(int fallback = 0) const { return static_cast<int>(as_number(fallback)); }
    std::string_view as_string(std::string_view fallback = {}) const;
    const std::vector<Json>& as_array() const { return array_; }
    const std::unordered_map<std::string, Json>& as_object() const { return object_; }

    // Object lookup; returns a shared null value when missing.
    const Json& operator[](std::string_view key) const;
    bool contains(std::string_view key) const;

    // Array helper with bounds checking.
    const Json& at(size_t index) const;

    static const Json& null_value();

private:
    friend class JsonParser;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::unordered_map<std::string, Json> object_;
};

} // namespace bge

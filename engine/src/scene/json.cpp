#include "bge/scene/json.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace bge {
namespace {

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

class JsonParser {
public:
    JsonParser(std::string_view text, std::string& error) : text_(text), error_(error) {}

    bool run(Json& out) {
        skip_ws();
        if (!parse_value(out, 0)) return false;
        skip_ws();
        if (pos_ != text_.size()) {
            fail("unexpected trailing content");
            return false;
        }
        return true;
    }

private:
    bool parse_value(Json& out, int depth) {
        if (depth > 128) return fail("JSON nesting too deep");
        skip_ws();
        if (pos_ >= text_.size()) return fail("unexpected end of file");

        char c = text_[pos_];
        if (c == '{') return parse_object(out, depth);
        if (c == '[') return parse_array(out, depth);
        if (c == '"') {
            out.type_ = Json::Type::String;
            return parse_string(out.string_);
        }
        if (c == 't') {
            if (!match("true")) return false;
            out.type_ = Json::Type::Bool;
            out.bool_ = true;
            return true;
        }
        if (c == 'f') {
            if (!match("false")) return false;
            out.type_ = Json::Type::Bool;
            out.bool_ = false;
            return true;
        }
        if (c == 'n') {
            if (!match("null")) return false;
            out.type_ = Json::Type::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            out.type_ = Json::Type::Number;
            return parse_number(out.number_);
        }
        return fail("unexpected character");
    }

    bool parse_object(Json& out, int depth) {
        out.type_ = Json::Type::Object;
        ++pos_; // {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < text_.size()) {
            skip_ws();
            std::string key;
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skip_ws();
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.object_[std::move(key)] = std::move(value);
            skip_ws();
            if (pos_ >= text_.size()) return fail("unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
        return fail("unterminated object");
    }

    bool parse_array(Json& out, int depth) {
        out.type_ = Json::Type::Array;
        ++pos_; // [
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < text_.size()) {
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.array_.push_back(std::move(value));
            skip_ws();
            if (pos_ >= text_.size()) return fail("unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
        return fail("unterminated array");
    }

    bool parse_string(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected string");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20u) return fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) return fail("unterminated escape");
            char e = text_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parse_hex4(cp)) return false;
                    if (cp >= 0xD800u && cp <= 0xDBFFu) {
                        if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                            return fail("missing low surrogate");
                        pos_ += 2;
                        uint32_t low = 0;
                        if (!parse_hex4(low)) return false;
                        if (low < 0xDC00u || low > 0xDFFFu) return fail("invalid low surrogate");
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return fail("invalid escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_hex4(uint32_t& out) {
        if (pos_ + 4 > text_.size()) return fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            int d = hex_digit(text_[pos_++]);
            if (d < 0) return fail("invalid \\u escape");
            out = (out << 4) | static_cast<uint32_t>(d);
        }
        return true;
    }

    bool parse_number(double& out) {
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) return fail("invalid number");
        std::string token(text_.substr(start, pos_ - start));
        char* end = nullptr;
        out = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0' || !std::isfinite(out)) {
            return fail("invalid number");
        }
        return true;
    }

    bool match(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool fail(const char* message) {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        error_ = std::string(message) + " at line " + std::to_string(line) + ", column " +
                 std::to_string(col);
        return false;
    }

    std::string_view text_;
    std::string& error_;
    size_t pos_ = 0;
};

bool Json::parse(std::string_view text, Json& out, std::string& error) {
    out = Json{};
    error.clear();
    JsonParser parser(text, error);
    return parser.run(out);
}

std::string_view Json::as_string(std::string_view fallback) const {
    return type_ == Type::String ? std::string_view(string_) : fallback;
}

const Json& Json::operator[](std::string_view key) const {
    if (type_ != Type::Object) return null_value();
    auto it = object_.find(std::string(key));
    return it == object_.end() ? null_value() : it->second;
}

bool Json::contains(std::string_view key) const {
    return type_ == Type::Object && object_.find(std::string(key)) != object_.end();
}

const Json& Json::at(size_t index) const {
    return index < array_.size() ? array_[index] : null_value();
}

const Json& Json::null_value() {
    static const Json null;
    return null;
}

} // namespace bge

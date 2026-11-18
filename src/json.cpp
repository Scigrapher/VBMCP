#include "json.h"
#include "common.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>

namespace mcpaddin {

Json::Json() : type_(Null), bool_(false), number_(0.0) {}
Json::Json(bool value) : type_(Bool), bool_(value), number_(0.0) {}
Json::Json(double value) : type_(Number), bool_(false), number_(value) {}
Json::Json(int value) : type_(Number), bool_(false), number_((double)value) {}
Json::Json(long value) : type_(Number), bool_(false), number_((double)value) {}
Json::Json(int64_t value) : type_(Number), bool_(false), number_((double)value) {}
Json::Json(const char* value) : type_(String), bool_(false), number_(0.0), string_(value ? value : "") {}
Json::Json(const std::string& value) : type_(String), bool_(false), number_(0.0), string_(value) {}
Json::Json(const std::wstring& value) : type_(String), bool_(false), number_(0.0), string_(WideToUtf8(value)) {}

Json Json::ObjectValue() {
    Json result;
    result.type_ = Object;
    return result;
}

Json Json::ArrayValue() {
    Json result;
    result.type_ = Array;
    return result;
}

Json Json::NullValue() {
    return Json();
}

Json Json::BoolValue(bool value) {
    return Json(value);
}

Json Json::NumberValue(double value) {
    return Json(value);
}

Json Json::StringValue(const std::string& value) {
    return Json(value);
}

Json Json::StringValue(const std::wstring& value) {
    return Json(value);
}

Json Json::ArrayValue(const std::vector<Json>& values) {
    Json result;
    result.type_ = Array;
    result.array_ = values;
    return result;
}

Json Json::ObjectValue(const std::map<std::string, Json>& values) {
    Json result;
    result.type_ = Object;
    result.object_ = values;
    return result;
}

bool Json::AsBool(bool defaultValue) const {
    if (type_ == Bool) return bool_;
    if (type_ == Number) return number_ != 0.0;
    return defaultValue;
}

double Json::AsDouble(double defaultValue) const {
    if (type_ == Number) return number_;
    if (type_ == Bool) return bool_ ? 1.0 : 0.0;
    return defaultValue;
}

int Json::AsInt(int defaultValue) const {
    if (type_ == Number || type_ == Bool) return (int)AsDouble();
    return defaultValue;
}

int64_t Json::AsInt64(int64_t defaultValue) const {
    if (type_ == Number || type_ == Bool) return (int64_t)AsDouble();
    return defaultValue;
}

std::string Json::AsString(const std::string& defaultValue) const {
    if (type_ == String) return string_;
    if (type_ == Number) {
        char buffer[64] = {0};
        if (number_ == std::floor(number_) && std::fabs(number_) < 9.0e15) {
            _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.0f", number_);
        } else {
            _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.17g", number_);
        }
        return buffer;
    }
    if (type_ == Bool) return bool_ ? "true" : "false";
    return defaultValue;
}

std::wstring Json::AsWString(const std::wstring& defaultValue) const {
    if (type_ == String || type_ == Number || type_ == Bool) return Utf8ToWide(AsString());
    return defaultValue;
}

size_t Json::size() const {
    if (type_ == Array) return array_.size();
    if (type_ == Object) return object_.size();
    return 0;
}

bool Json::empty() const {
    return size() == 0;
}

void Json::clear() {
    type_ = Null;
    bool_ = false;
    number_ = 0.0;
    string_.clear();
    array_.clear();
    object_.clear();
}

bool Json::Has(const std::string& key) const {
    return type_ == Object && object_.find(key) != object_.end();
}

const Json& Json::Get(const std::string& key) const {
    static const Json nullValue;
    if (type_ != Object) return nullValue;
    std::map<std::string, Json>::const_iterator it = object_.find(key);
    return it == object_.end() ? nullValue : it->second;
}

Json& Json::Get(const std::string& key) {
    if (type_ != Object) {
        clear();
        type_ = Object;
    }
    return object_[key];
}

void Json::Set(const std::string& key, const Json& value) {
    if (type_ != Object) {
        clear();
        type_ = Object;
    }
    object_[key] = value;
}

void Json::Remove(const std::string& key) {
    if (type_ == Object) object_.erase(key);
}

void Json::push_back(const Json& value) {
    if (type_ != Array) {
        clear();
        type_ = Array;
    }
    array_.push_back(value);
}

const Json& Json::at(size_t index) const {
    static const Json nullValue;
    if (type_ != Array || index >= array_.size()) return nullValue;
    return array_[index];
}

Json& Json::at(size_t index) {
    if (type_ != Array) {
        clear();
        type_ = Array;
    }
    if (index >= array_.size()) array_.resize(index + 1);
    return array_[index];
}

namespace {

class JsonParser {
public:
    JsonParser(const std::string& text) : text_(text), pos_(0), depth_(0) {}

    bool Parse(Json& out, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(out, error)) return false;
        SkipWhitespace();
        if (pos_ != text_.size()) {
            error = "Unexpected trailing characters at byte " + ToString(pos_);
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    size_t pos_;
    int depth_;

    static std::string ToString(size_t value) {
        char buffer[40] = {0};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%llu", (unsigned long long)value);
        return buffer;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            char ch = text_[pos_];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') ++pos_;
            else break;
        }
    }

    bool Fail(std::string& error, const char* message) {
        error = std::string(message) + " at byte " + ToString(pos_);
        return false;
    }

    bool ParseValue(Json& out, std::string& error) {
        if (++depth_ > 256) {
            --depth_;
            return Fail(error, "JSON nesting is too deep");
        }
        SkipWhitespace();
        if (pos_ >= text_.size()) {
            --depth_;
            return Fail(error, "Unexpected end of JSON");
        }
        char ch = text_[pos_];
        bool ok = false;
        if (ch == 'n') ok = ParseLiteral("null", Json::NullValue(), out, error);
        else if (ch == 't') ok = ParseLiteral("true", Json::BoolValue(true), out, error);
        else if (ch == 'f') ok = ParseLiteral("false", Json::BoolValue(false), out, error);
        else if (ch == '"') {
            std::string value;
            ok = ParseString(value, error);
            if (ok) out = Json::StringValue(value);
        } else if (ch == '{') {
            ok = ParseObject(out, error);
        } else if (ch == '[') {
            ok = ParseArray(out, error);
        } else if (ch == '-' || (ch >= '0' && ch <= '9')) {
            ok = ParseNumber(out, error);
        } else {
            ok = Fail(error, "Unexpected character in JSON");
        }
        --depth_;
        return ok;
    }

    bool ParseLiteral(const char* literal, const Json& value, Json& out, std::string& error) {
        size_t len = strlen(literal);
        if (text_.compare(pos_, len, literal) != 0) return Fail(error, "Invalid JSON literal");
        pos_ += len;
        out = value;
        return true;
    }

    bool ParseObject(Json& out, std::string& error) {
        ++pos_;
        out = Json::ObjectValue();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return Fail(error, "Expected object key");
            std::string key;
            if (!ParseString(key, error)) return false;
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return Fail(error, "Expected ':' after object key");
            ++pos_;
            Json value;
            if (!ParseValue(value, error)) return false;
            out.Set(key, value);
            SkipWhitespace();
            if (pos_ >= text_.size()) return Fail(error, "Unterminated object");
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (text_[pos_] != ',') return Fail(error, "Expected ',' or '}' in object");
            ++pos_;
        }
    }

    bool ParseArray(Json& out, std::string& error) {
        ++pos_;
        out = Json::ArrayValue();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            Json value;
            if (!ParseValue(value, error)) return false;
            out.push_back(value);
            SkipWhitespace();
            if (pos_ >= text_.size()) return Fail(error, "Unterminated array");
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (text_[pos_] != ',') return Fail(error, "Expected ',' or ']' in array");
            ++pos_;
        }
    }

    bool ParseHex4(uint32_t& value, std::string& error) {
        if (pos_ + 4 > text_.size()) return Fail(error, "Incomplete Unicode escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            char ch = text_[pos_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= (uint32_t)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= (uint32_t)(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= (uint32_t)(ch - 'A' + 10);
            else return Fail(error, "Invalid Unicode escape");
        }
        return true;
    }

    bool ParseString(std::string& out, std::string& error) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return Fail(error, "Expected string");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            unsigned char ch = (unsigned char)text_[pos_++];
            if (ch == '"') return true;
            if (ch < 0x20) return Fail(error, "Control character in string");
            if (ch != '\\') {
                out.push_back((char)ch);
                continue;
            }
            if (pos_ >= text_.size()) return Fail(error, "Incomplete string escape");
            char escape = text_[pos_++];
            switch (escape) {
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
                    if (!ParseHex4(cp, error)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        size_t save = pos_;
                        if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = 0;
                            if (!ParseHex4(low, error)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                pos_ = save;
                            }
                        }
                    }
                    Json::AppendUtf8(out, cp);
                    break;
                }
                default:
                    return Fail(error, "Invalid string escape");
            }
        }
        return Fail(error, "Unterminated string");
    }

    bool ParseNumber(Json& out, std::string& error) {
        size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return Fail(error, "Incomplete number");
        if (text_[pos_] == '0') {
            ++pos_;
            if (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                return Fail(error, "Leading zero in number");
            }
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        } else {
            return Fail(error, "Invalid number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                return Fail(error, "Invalid fraction");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                return Fail(error, "Invalid exponent");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        std::string number = text_.substr(start, pos_ - start);
        char* end = NULL;
        errno = 0;
        double value = strtod(number.c_str(), &end);
        if (errno == ERANGE || end == NULL || *end != '\0' || !std::isfinite(value)) {
            return Fail(error, "Number is out of range");
        }
        out = Json::NumberValue(value);
        return true;
    }
};

} // namespace

Json Json::Parse(const std::string& text, std::string* error) {
    Json result;
    std::string localError;
    JsonParser parser(text);
    if (!parser.Parse(result, localError)) {
        if (error) *error = localError;
        return Json();
    }
    if (error) error->clear();
    return result;
}

bool Json::ParseInPlace(const std::string& text, std::string* error) {
    std::string localError;
    Json result;
    JsonParser parser(text);
    if (!parser.Parse(result, localError)) {
        if (error) *error = localError;
        return false;
    }
    *this = result;
    if (error) error->clear();
    return true;
}

void Json::AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

std::string Json::Dump(bool pretty, int indent) const {
    std::string out;
    DumpTo(out, pretty, indent, 0);
    return out;
}

static void AppendIndent(std::string& out, bool pretty, int indent, int depth) {
    if (!pretty) return;
    out.push_back('\n');
    out.append((size_t)(indent * depth), ' ');
}

static void AppendJsonString(std::string& out, const std::string& value) {
    out.push_back('"');
    for (size_t i = 0; i < value.size(); ++i) {
        unsigned char ch = (unsigned char)value[i];
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char escaped[8] = {0};
                    _snprintf_s(escaped, sizeof(escaped), _TRUNCATE, "\\u%04x", (unsigned int)ch);
                    out += escaped;
                } else {
                    out.push_back((char)ch);
                }
                break;
        }
    }
    out.push_back('"');
}

void Json::DumpTo(std::string& out, bool pretty, int indent, int depth) const {
    switch (type_) {
        case Null:
            out += "null";
            break;
        case Bool:
            out += bool_ ? "true" : "false";
            break;
        case Number: {
            char buffer[64] = {0};
            if (number_ == std::floor(number_) && std::fabs(number_) < 9.0e15) {
                _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.0f", number_);
            } else {
                _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%.17g", number_);
            }
            out += buffer;
            break;
        }
        case String:
            AppendJsonString(out, string_);
            break;
        case Array: {
            out.push_back('[');
            for (size_t i = 0; i < array_.size(); ++i) {
                if (i) out.push_back(',');
                AppendIndent(out, pretty, indent, depth + 1);
                array_[i].DumpTo(out, pretty, indent, depth + 1);
            }
            if (!array_.empty()) AppendIndent(out, pretty, indent, depth);
            out.push_back(']');
            break;
        }
        case Object: {
            out.push_back('{');
            size_t index = 0;
            for (std::map<std::string, Json>::const_iterator it = object_.begin(); it != object_.end(); ++it, ++index) {
                if (index) out.push_back(',');
                AppendIndent(out, pretty, indent, depth + 1);
                AppendJsonString(out, it->first);
                out += pretty ? ": " : ":";
                it->second.DumpTo(out, pretty, indent, depth + 1);
            }
            if (!object_.empty()) AppendIndent(out, pretty, indent, depth);
            out.push_back('}');
            break;
        }
    }
}

bool Json::operator==(const Json& other) const {
    if (type_ != other.type_) return false;
    switch (type_) {
        case Null: return true;
        case Bool: return bool_ == other.bool_;
        case Number: return number_ == other.number_;
        case String: return string_ == other.string_;
        case Array: return array_ == other.array_;
        case Object: return object_ == other.object_;
    }
    return false;
}

} // namespace mcpaddin

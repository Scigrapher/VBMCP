#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdint.h>

namespace mcpaddin {

class Json {
public:
    enum Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Json();
    explicit Json(bool value);
    explicit Json(double value);
    explicit Json(int value);
    explicit Json(long value);
    explicit Json(int64_t value);
    explicit Json(const char* value);
    explicit Json(const std::string& value);
    explicit Json(const std::wstring& value);

    static Json Parse(const std::string& text, std::string* error = NULL);
    static Json ObjectValue();
    static Json ArrayValue();
    static Json NullValue();
    static Json BoolValue(bool value);
    static Json NumberValue(double value);
    static Json StringValue(const std::string& value);
    static Json StringValue(const std::wstring& value);
    static Json ArrayValue(const std::vector<Json>& values);
    static Json ObjectValue(const std::map<std::string, Json>& values);

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Null; }
    bool IsBool() const { return type_ == Bool; }
    bool IsNumber() const { return type_ == Number; }
    bool IsString() const { return type_ == String; }
    bool IsArray() const { return type_ == Array; }
    bool IsObject() const { return type_ == Object; }

    bool AsBool(bool defaultValue = false) const;
    double AsDouble(double defaultValue = 0.0) const;
    int AsInt(int defaultValue = 0) const;
    int64_t AsInt64(int64_t defaultValue = 0) const;
    std::string AsString(const std::string& defaultValue = std::string()) const;
    std::wstring AsWString(const std::wstring& defaultValue = std::wstring()) const;

    size_t size() const;
    bool empty() const;
    void clear();

    bool Has(const std::string& key) const;
    const Json& Get(const std::string& key) const;
    Json& Get(const std::string& key);
    void Set(const std::string& key, const Json& value);
    void Remove(const std::string& key);
    const std::map<std::string, Json>& object() const { return object_; }
    std::map<std::string, Json>& object() { return object_; }

    const std::vector<Json>& array() const { return array_; }
    std::vector<Json>& array() { return array_; }
    void push_back(const Json& value);
    const Json& at(size_t index) const;
    Json& at(size_t index);

    std::string Dump(bool pretty = false, int indent = 0) const;
    bool ParseInPlace(const std::string& text, std::string* error = NULL);
    static void AppendUtf8(std::string& out, uint32_t codepoint);

    bool operator==(const Json& other) const;

private:
    Type type_;
    bool bool_;
    double number_;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;

    void DumpTo(std::string& out, bool pretty, int indent, int depth) const;
};

} // namespace mcpaddin

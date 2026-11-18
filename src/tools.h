#pragma once

#include "json.h"

#include <string>
#include <vector>

namespace mcpaddin {

struct ToolResult {
    bool ok;
    Json data;
    std::string errorCode;
    ToolResult() : ok(false), data(Json::ObjectValue()), errorCode("tool_error") {}

    static ToolResult Success(const Json& value) {
        ToolResult result;
        result.ok = true;
        result.data = value;
        return result;
    }
    static ToolResult Failure(const std::string& message, const std::string& code = "tool_error") {
        ToolResult result;
        result.ok = false;
        result.errorCode = code;
        result.data = Json::ObjectValue();
        result.data.Set("error", Json::StringValue(message));
        result.data.Set("code", Json::StringValue(code));
        return result;
    }
};

class IIdeBackend {
public:
    virtual ~IIdeBackend() {}
    virtual ToolResult CallTool(const std::string& name, const Json& arguments) = 0;
    virtual Json Status() = 0;
    virtual bool IsAvailable() = 0;
    virtual std::string BackendName() = 0;
};

Json GetToolDefinitions();
bool IsKnownTool(const std::string& name);

Json MakeObject();
Json MakeArray();
Json MakeString(const char* value);
Json MakeString(const std::string& value);
Json MakeNumber(double value);
Json MakeBool(bool value);
Json SchemaObject(const std::map<std::string, Json>& properties,
                  const std::vector<std::string>& required);
Json SchemaString(const std::string& description,
                  const std::vector<std::string>& enumValues = std::vector<std::string>());
Json SchemaBoolean(const std::string& description);
Json SchemaInteger(const std::string& description,
                   const Json& minimum = Json::NullValue(),
                   const Json& maximum = Json::NullValue());
Json SchemaObjectMap(const std::string& description);
Json SchemaArrayOfStrings(const std::string& description);

} // namespace mcpaddin

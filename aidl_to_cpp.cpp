/*
 * Copyright (C) 2018, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "aidl_to_cpp.h"
#include "aidl_language.h"
#include "logging.h"

#include <functional>
#include <unordered_map>

using std::ostringstream;

namespace android {
namespace aidl {
namespace cpp {

std::string ConstantValueDecorator(const AidlTypeSpecifier& type, const std::string& raw_value) {
  if (type.GetName() == "String" && !type.IsArray() && !type.IsUtf8InCpp()) {
    return "::android::String16(" + raw_value + ")";
  }

  return raw_value;
};

struct TypeInfo {
  // name of the type in C++ output
  std::string cpp_name;

  // function that writes an expression to convert a variable to a Json::Value
  // object
  std::function<void(const CodeGeneratorContext& c, const string& var_name)> toJsonValueExpr;
};

const static std::unordered_map<std::string, TypeInfo> kTypeInfoMap = {
    {"void", {"void", nullptr}},
    {"boolean",
     {
         "bool",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(" << var_name << "? \"true\" : \"false\")";
         },
     }},
    {"byte",
     {
         "int8_t",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(" << var_name << ")";
         },
     }},
    {"char",
     {
         "char16_t",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(std::string(android::String8(&" << var_name << ", 1)))";
         },
     }},
    {"int",
     {
         "int32_t",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(" << var_name << ")";
         },
     }},
    {"long",
     {
         "int64_t",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(static_cast<Json::Int64>(" << var_name << "))";
         },
     }},
    {"float",
     {
         "float",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(" << var_name << ")";
         },
     }},
    {"double",
     {
         "double",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(" << var_name << ")";
         },
     }},
    {"String",
     {
         "std::string",
         [](const CodeGeneratorContext& c, const string& var_name) {
           c.writer << "Json::Value(" << var_name << ")";
         },
     }}
    // missing List, Map, ParcelFileDescriptor, IBinder
};

TypeInfo GetTypeInfo(const AidlTypenames&, const AidlTypeSpecifier& aidl) {
  CHECK(aidl.IsResolved()) << aidl.ToString();
  const string& aidl_name = aidl.GetName();

  TypeInfo info;
  if (AidlTypenames::IsBuiltinTypename(aidl_name)) {
    auto it = kTypeInfoMap.find(aidl_name);
    if (it != kTypeInfoMap.end()) {
      info = it->second;
    }
  }
  // Missing interface and parcelable type
  return info;
}

void WriteLogFor(const CodeGeneratorContext& c) {
  const TypeInfo info = GetTypeInfo(c.types, c.type);
  if (info.cpp_name == "") {
    return;
  }

  const string var_object_expr = ((c.isPointer ? "*" : "")) + c.name;
  if (c.type.IsArray()) {
    c.writer << "for (const auto& v: " << var_object_expr << ") " << c.log << "[\"" << c.name
             << "\"] = ";
    info.toJsonValueExpr(c, "v");
    c.writer << ";";
  } else {
    c.writer << c.log << "[\"" << c.name << "\"] = ";
    info.toJsonValueExpr(c, var_object_expr);
    c.writer << ";";
  }
  c.writer << "\n";
}

std::string GetTransactionIdFor(const AidlMethod& method) {
  ostringstream output;

  if (method.IsUserDefined()) {
    output << "::android::IBinder::FIRST_CALL_TRANSACTION + ";
  }
  output << method.GetId() << " /* " << method.GetName() << " */";
  return output.str();
}
}  // namespace cpp
}  // namespace aidl
}  // namespace android

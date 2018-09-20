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
 * limitations under the License.
 */

#include "aidl_to_ndk.h"
#include "aidl_language.h"
#include "aidl_to_cpp_common.h"
#include "logging.h"

#include <android-base/strings.h>

#include <functional>

using ::android::base::Join;
using ::android::base::Split;

namespace android {
namespace aidl {
namespace ndk {

struct TypeInfo {
  // name of the type in C++ output
  std::string cpp_name;
  // whether to prefer 'value type' over 'const&'
  bool value_is_cheap;

  std::function<void(const CodeGeneratorContext& c)> readParcelFunction;
  std::function<void(const CodeGeneratorContext& c)> writeParcelFunction;
};

static std::function<void(const CodeGeneratorContext& c)> standardRead(const std::string& name) {
  return [name](const CodeGeneratorContext& c) {
    c.writer << "AParcel_read" << name << "(" << c.parcel << ", " << c.var << ")";
  };
}
static std::function<void(const CodeGeneratorContext& c)> standardWrite(const std::string& name) {
  return [name](const CodeGeneratorContext& c) {
    c.writer << "AParcel_write" << name << "(" << c.parcel << ", " << c.var << ")";
  };
}

// map from AIDL built-in type name to the corresponding Ndk type name
static map<std::string, TypeInfo> kNdkTypeInfoMap = {
    {"void", {"void", true, nullptr, nullptr}},
    {"boolean", {"bool", true, standardRead("Bool"), standardWrite("Bool")}},
    {"byte", {"int8_t", true, standardRead("Byte"), standardWrite("Byte")}},
    {"char", {"char16_t", true, standardRead("Char"), standardWrite("Char")}},
    {"int", {"int32_t", true, standardRead("Int32"), standardWrite("Int32")}},
    {"long", {"int64_t", true, standardRead("Int64"), standardWrite("Int64")}},
    {"float", {"float", true, standardRead("Float"), standardWrite("Float")}},
    {"double", {"double", true, standardRead("Double"), standardWrite("Double")}},
    // TODO(b/111445392) {"String", ""},
    // TODO(b/111445392) {"List", ""},
    // TODO(b/111445392) {"Map", ""},
    {"IBinder",
     {"::android::AutoAIBinder", false,
      [](const CodeGeneratorContext& c) {
        c.writer << "AParcel_readNullableStrongBinder(" << c.parcel << ", (" << c.var
                 << ")->getR())";
      },
      [](const CodeGeneratorContext& c) {
        c.writer << "AParcel_writeStrongBinder(" << c.parcel << ", " << c.var << ".get())";
      }}},
    // TODO(b/111445392) {"FileDescriptor", ""},
    // TODO(b/111445392) {"CharSequence", ""},
};

std::string NdkNameOf(const AidlTypeSpecifier& aidl, StorageMode mode) {
  CHECK(aidl.IsResolved()) << aidl.ToString();

  // TODO(112664205): this is okay for some types
  CHECK(!aidl.IsGeneric()) << aidl.ToString();
  // TODO(112664205): need to support array types
  CHECK(!aidl.IsArray()) << aidl.ToString();

  const string aidl_name = aidl.GetName();
  TypeInfo info;
  if (AidlTypenames::IsBuiltinTypename(aidl_name)) {
    auto it = kNdkTypeInfoMap.find(aidl_name);
    CHECK(it != kNdkTypeInfoMap.end());
    info = it->second;
  } else {
    info = {"::aidl::" + Join(Split(aidl_name, "."), "::"), false, nullptr, nullptr};
  }

  switch (mode) {
    case StorageMode::STACK:
      return info.cpp_name;
    case StorageMode::ARGUMENT:
      if (info.value_is_cheap) {
        return info.cpp_name;
      } else {
        return "const " + info.cpp_name + "&";
      }
    case StorageMode::OUT_ARGUMENT:
      return info.cpp_name + "*";
    default:
      AIDL_FATAL(aidl.GetName()) << "Unrecognized mode type: " << static_cast<int>(mode);
  }
}

void WriteToParcelFor(const CodeGeneratorContext& c) {
  const string aidl_name = c.type.GetName();
  if (AidlTypenames::IsBuiltinTypename(aidl_name)) {
    auto it = kNdkTypeInfoMap.find(aidl_name);
    CHECK(it != kNdkTypeInfoMap.end());
    const TypeInfo& info = it->second;

    info.writeParcelFunction(c);
  } else {
    AIDL_FATAL("TODO(b/112664205): not yet supported");
  }
}

void ReadFromParcelFor(const CodeGeneratorContext& c) {
  const string aidl_name = c.type.GetName();
  if (AidlTypenames::IsBuiltinTypename(aidl_name)) {
    auto it = kNdkTypeInfoMap.find(aidl_name);
    CHECK(it != kNdkTypeInfoMap.end());
    const TypeInfo& info = it->second;

    info.readParcelFunction(c);
  } else {
    AIDL_FATAL("TODO(b/112664205): not yet supported");
  }
}

std::string NdkArgListOf(const AidlMethod& method) {
  std::vector<std::string> method_arguments;
  for (const auto& a : method.GetArguments()) {
    StorageMode mode = a->IsOut() ? StorageMode::OUT_ARGUMENT : StorageMode::ARGUMENT;
    std::string type = NdkNameOf(a->GetType(), mode);
    std::string name = cpp::BuildVarName(*a);
    method_arguments.emplace_back(type + " " + name);
  }

  if (method.GetType().GetName() != "void") {
    std::string return_type = NdkNameOf(method.GetType(), StorageMode::OUT_ARGUMENT);
    method_arguments.emplace_back(return_type + " _aidl_return");
  }

  return Join(method_arguments, ", ");
}

std::string NdkCallListFor(const AidlMethod& method) {
  std::vector<std::string> method_arguments;
  for (const auto& a : method.GetArguments()) {
    std::string reference_prefix = a->IsOut() ? "&" : "";
    std::string name = cpp::BuildVarName(*a);
    method_arguments.emplace_back(reference_prefix + name);
  }

  if (method.GetType().GetName() != "void") {
    method_arguments.emplace_back("&_aidl_return");
  }

  return Join(method_arguments, ", ");
}

std::string NdkMethodDecl(const AidlMethod& method, const std::string& clazz) {
  std::string class_prefix = clazz.empty() ? "" : (clazz + "::");
  return "::android::AutoAStatus " + class_prefix + method.GetName() + "(" + NdkArgListOf(method) +
         ")";
}

}  // namespace ndk
}  // namespace aidl
}  // namespace android

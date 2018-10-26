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
#include "os.h"

#include <android-base/strings.h>

#include <functional>

using ::android::base::Join;
using ::android::base::Split;

namespace android {
namespace aidl {
namespace ndk {

std::string NdkHeaderFile(const AidlDefinedType& defined_type, cpp::ClassNames name,
                          bool use_os_sep) {
  char seperator = (use_os_sep) ? OS_PATH_SEPARATOR : '/';
  return std::string("aidl") + seperator + cpp::HeaderFile(defined_type, name, use_os_sep);
}

struct TypeInfo {
  // name of the type in C++ output
  std::string cpp_name;
  // whether to prefer 'value type' over 'const&'
  bool value_is_cheap;

  std::function<void(const CodeGeneratorContext& c)> readParcelFunction;
  std::function<void(const CodeGeneratorContext& c)> writeParcelFunction;

  std::function<void(const CodeGeneratorContext& c)> readArrayParcelFunction;
  std::function<void(const CodeGeneratorContext& c)> writeArrayParcelFunction;
};

static std::function<void(const CodeGeneratorContext& c)> StandardRead(const std::string& name) {
  return [name](const CodeGeneratorContext& c) {
    c.writer << name << "(" << c.parcel << ", " << c.var << ")";
  };
}
static std::function<void(const CodeGeneratorContext& c)> StandardWrite(const std::string& name) {
  return [name](const CodeGeneratorContext& c) {
    c.writer << name << "(" << c.parcel << ", " << c.var << ")";
  };
}

TypeInfo PrimitiveType(const std::string& cpp_name, const std::string& pretty_name) {
  return TypeInfo{
      .cpp_name = cpp_name,
      .value_is_cheap = true,
      .readParcelFunction = StandardRead("AParcel_read" + pretty_name),
      .writeParcelFunction = StandardWrite("AParcel_write" + pretty_name),
      .readArrayParcelFunction = StandardRead("::ndk::AParcel_readVector"),
      .writeArrayParcelFunction = StandardWrite("::ndk::AParcel_writeVector"),
  };
}

// map from AIDL built-in type name to the corresponding Ndk type name
static map<std::string, TypeInfo> kNdkTypeInfoMap = {
    {"void", {"void", true, nullptr, nullptr, nullptr, nullptr}},
    {"boolean", PrimitiveType("bool", "Bool")},
    {"byte", PrimitiveType("int8_t", "Byte")},
    {"char", PrimitiveType("char16_t", "Char")},
    {"int", PrimitiveType("int32_t", "Int32")},
    {"long", PrimitiveType("int64_t", "Int64")},
    {"float", PrimitiveType("float", "Float")},
    {"double", PrimitiveType("double", "Double")},
    {"String",
     {"std::string", false, StandardRead("::ndk::AParcel_readString"),
      StandardWrite("::ndk::AParcel_writeString"), nullptr, nullptr}},
    // TODO(b/111445392) {"List", ""},
    // TODO(b/111445392) {"Map", ""},
    {
        "IBinder",
        {
            "::ndk::SpAIBinder",
            false,
            [](const CodeGeneratorContext& c) {
              c.writer << "AParcel_readNullableStrongBinder(" << c.parcel << ", (" << c.var
                       << ")->getR())";
            },
            [](const CodeGeneratorContext& c) {
              c.writer << "AParcel_writeStrongBinder(" << c.parcel << ", " << c.var << ".get())";
            },
            nullptr,
            nullptr,
        },
    },
    // TODO(b/111445392) {"FileDescriptor", ""},
    // TODO(b/111445392) {"CharSequence", ""},
};

TypeInfo GetTypeInfo(const AidlTypenames& types, const AidlTypeSpecifier& aidl) {
  CHECK(aidl.IsResolved()) << aidl.ToString();

  const string aidl_name = aidl.GetName();

  // TODO(b/112664205): this is okay for some types
  AIDL_FATAL_IF(aidl.IsGeneric(), aidl) << aidl.ToString();
  // TODO(b/112664205): this is okay for some types
  AIDL_FATAL_IF(aidl.IsNullable(), aidl) << aidl.ToString();

  // @utf8InCpp can only be used on String. It only matters for the CPP backend, not the NDK
  // backend.
  AIDL_FATAL_IF(aidl.IsUtf8InCpp() && aidl_name != "String", aidl) << aidl.ToString();

  TypeInfo info;
  if (AidlTypenames::IsBuiltinTypename(aidl_name)) {
    auto it = kNdkTypeInfoMap.find(aidl_name);
    CHECK(it != kNdkTypeInfoMap.end());
    info = it->second;
  } else {
    const AidlDefinedType* type = types.TryGetDefinedType(aidl_name);

    AIDL_FATAL_IF(type == nullptr, aidl_name) << "Unrecognized type.";

    if (type->AsInterface() != nullptr) {
      const std::string clazz = NdkFullClassName(*type, cpp::ClassNames::INTERFACE);
      info = TypeInfo{
          .cpp_name = "std::shared_ptr<" + clazz + ">",
          .value_is_cheap = false,
          .readParcelFunction = StandardRead(clazz + "::readFromParcel"),
          .writeParcelFunction = StandardWrite(clazz + "::writeToParcel"),
      };
    } else if (type->AsParcelable() != nullptr) {
      info = TypeInfo{
          .cpp_name = NdkFullClassName(*type, cpp::ClassNames::BASE),
          .value_is_cheap = false,
          .readParcelFunction =
              [](const CodeGeneratorContext& c) {
                c.writer << "(" << c.var << ")->readFromParcel(" << c.parcel << ")";
              },
          .writeParcelFunction =
              [](const CodeGeneratorContext& c) {
                c.writer << "(" << c.var << ").writeToParcel(" << c.parcel << ")";
              },
      };
    } else {
      AIDL_FATAL(aidl_name) << "Unrecognized type";
    }
  }

  AIDL_FATAL_IF(aidl.IsArray() && info.readArrayParcelFunction == nullptr, aidl) << aidl.ToString();
  AIDL_FATAL_IF(aidl.IsArray() && info.writeArrayParcelFunction == nullptr, aidl)
      << aidl.ToString();

  return info;
}

std::string NdkFullClassName(const AidlDefinedType& type, cpp::ClassNames name) {
  std::vector<std::string> pieces = {"::aidl"};
  std::vector<std::string> package = type.GetSplitPackage();
  pieces.insert(pieces.end(), package.begin(), package.end());
  pieces.push_back(cpp::ClassName(type, name));

  return Join(pieces, "::");
}

std::string NdkNameOf(const AidlTypenames& types, const AidlTypeSpecifier& aidl, StorageMode mode) {
  TypeInfo info = GetTypeInfo(types, aidl);

  bool value_is_cheap = info.value_is_cheap;
  std::string cpp_name = info.cpp_name;

  if (aidl.IsArray()) {
    value_is_cheap = false;
    cpp_name = "std::vector<" + info.cpp_name + ">";
  }

  switch (mode) {
    case StorageMode::STACK:
      return cpp_name;
    case StorageMode::ARGUMENT:
      if (value_is_cheap) {
        return cpp_name;
      } else {
        return "const " + cpp_name + "&";
      }
    case StorageMode::OUT_ARGUMENT:
      return cpp_name + "*";
    default:
      AIDL_FATAL(aidl.GetName()) << "Unrecognized mode type: " << static_cast<int>(mode);
  }
}

void WriteToParcelFor(const CodeGeneratorContext& c) {
  TypeInfo info = GetTypeInfo(c.types, c.type);

  if (c.type.IsArray()) {
    AIDL_FATAL_IF(info.writeArrayParcelFunction == nullptr, c.type)
        << "Type does not support writing arrays.";
    info.writeArrayParcelFunction(c);
  } else {
    AIDL_FATAL_IF(info.writeParcelFunction == nullptr, c.type) << "Type does not support writing.";
    info.writeParcelFunction(c);
  }
}

void ReadFromParcelFor(const CodeGeneratorContext& c) {
  TypeInfo info = GetTypeInfo(c.types, c.type);

  if (c.type.IsArray()) {
    AIDL_FATAL_IF(info.readArrayParcelFunction == nullptr, c.type)
        << "Type does not support reading arrays.";
    info.readArrayParcelFunction(c);
  } else {
    AIDL_FATAL_IF(info.readParcelFunction == nullptr, c.type) << "Type does not support reading.";
    info.readParcelFunction(c);
  }
}

std::string NdkArgListOf(const AidlTypenames& types, const AidlMethod& method) {
  std::vector<std::string> method_arguments;
  for (const auto& a : method.GetArguments()) {
    StorageMode mode = a->IsOut() ? StorageMode::OUT_ARGUMENT : StorageMode::ARGUMENT;
    std::string type = NdkNameOf(types, a->GetType(), mode);
    std::string name = cpp::BuildVarName(*a);
    method_arguments.emplace_back(type + " " + name);
  }

  if (method.GetType().GetName() != "void") {
    std::string return_type = NdkNameOf(types, method.GetType(), StorageMode::OUT_ARGUMENT);
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

std::string NdkMethodDecl(const AidlTypenames& types, const AidlMethod& method,
                          const std::string& clazz) {
  std::string class_prefix = clazz.empty() ? "" : (clazz + "::");
  return "::ndk::ScopedAStatus " + class_prefix + method.GetName() + "(" +
         NdkArgListOf(types, method) + ")";
}

}  // namespace ndk
}  // namespace aidl
}  // namespace android

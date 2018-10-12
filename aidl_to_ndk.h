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
#pragma once

#include "aidl_language.h"
#include "aidl_to_cpp_common.h"

namespace android {
namespace aidl {
namespace ndk {

enum class StorageMode {
  STACK,
  ARGUMENT,      // Value for primitives, const& for larger types
  OUT_ARGUMENT,  // Pointer to raw type
};

std::string NdkHeaderFile(const AidlDefinedType& defined_type, cpp::ClassNames name,
                          bool use_os_sep = true);

// Returns ::aidl::some_package::some_sub_package::foo::IFoo/BpFoo/BnFoo
std::string NdkFullClassName(const AidlDefinedType& type, cpp::ClassNames name);

// Returns the corresponding Ndk type name for an AIDL type spec including
// array modifiers.
std::string NdkNameOf(const AidlTypenames& types, const AidlTypeSpecifier& aidl, StorageMode mode);

struct CodeGeneratorContext {
  CodeWriter& writer;

  const AidlTypenames& types;
  const AidlTypeSpecifier& type;

  const string parcel;
  const string var;
};

void WriteToParcelFor(const CodeGeneratorContext& c);
void ReadFromParcelFor(const CodeGeneratorContext& c);

// -> 'type name, type name, type name' for a method
std::string NdkArgListOf(const AidlTypenames& types, const AidlMethod& method);

// -> 'name, name, name' for a method where out arguments are '&name'
std::string NdkCallListFor(const AidlMethod& method);

// -> 'status (class::)name(type name, ...)' for a method
std::string NdkMethodDecl(const AidlTypenames& types, const AidlMethod& method,
                          const std::string& clazz = "");

}  // namespace ndk
}  // namespace aidl
}  // namespace android

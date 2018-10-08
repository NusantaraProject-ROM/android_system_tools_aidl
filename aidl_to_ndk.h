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

namespace android {
namespace aidl {
namespace ndk {

enum class StorageMode {
  STACK,
  ARGUMENT,      // Value for primitives, const& for larger types
  OUT_ARGUMENT,  // Pointer to raw type
};

// Returns the corresponding Ndk type name for an AIDL type spec including
// array modifiers.
std::string NdkNameOf(const AidlTypeSpecifier& aidl, StorageMode mode);

struct CodeGeneratorContext {
  CodeWriter& writer;
  const AidlTypeSpecifier& type;
  const string parcel;
  const string var;
};

void WriteToParcelFor(const CodeGeneratorContext& c);
void ReadFromParcelFor(const CodeGeneratorContext& c);

}  // namespace ndk
}  // namespace aidl
}  // namespace android

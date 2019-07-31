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
namespace cpp {

// This header provides functions that translate AIDL things to cpp things.

std::string ConstantValueDecorator(const AidlTypeSpecifier& type, const std::string& raw_value);

struct CodeGeneratorContext {
  CodeWriter& writer;

  const AidlTypenames& types;
  const AidlTypeSpecifier& type;  // an argument or return type to generate code for
  const string name;              // name of the variable for the argument or the return value
  const bool isPointer;           // whether the variable 'name' is a pointer or not
  const string log;               // name of the variable of type Json::Value to write the log into
};

std::string GetTransactionIdFor(const AidlMethod& method);

std::string CppNameOf(const AidlTypeSpecifier& type, const AidlTypenames& typenames);

std::string ParcelReadMethodOf(const AidlTypeSpecifier& type, const AidlTypenames& typenames);

std::string ParcelWriteMethodOf(const AidlTypeSpecifier& type, const AidlTypenames& typenames);

void AddHeaders(const AidlTypeSpecifier& type, const AidlTypenames& typenames,
                std::set<std::string>& headers);

void AddHeaders(const AidlDefinedType& parcelable, std::set<std::string>& headers);

std::string CastOf(const AidlTypeSpecifier& raw_type, const AidlTypenames& typenames,
                   const std::string var);
}  // namespace cpp
}  // namespace aidl
}  // namespace android

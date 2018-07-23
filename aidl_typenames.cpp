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

#include "aidl_typenames.h"
#include "aidl_language.h"
#include "logging.h"

#include <android-base/strings.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using android::base::EndsWith;
using android::base::Join;
using android::base::Split;
using android::base::Trim;

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

namespace android {
namespace aidl {

// The built-in AIDL types..
static const set<string> kBuiltinTypes = {
    "void",           "boolean",      "byte",           "char",         "int", "long",
    "float",          "double",       "String",         "List",         "Map", "IBinder",
    "FileDescriptor", "CharSequence"};

// Note: these types may look wrong because they look like Java
// types, but they have long been supported from the time when Java
// was the only target language of this compiler. They are added here for
// backwards compatibility, but we internally treat them as List and Map,
// respectively.
static const map<string, string> kJavaLikeTypeToAidlType = {
  {"java.util.List", "List"},
  {"java.util.Map", "Map"},
};

bool AidlTypenames::AddDefinedType(const AidlDefinedType* type) {
  const string name = type->GetCanonicalName();
  if (defined_types_.find(name) != defined_types_.end()) {
    return false;
  }
  defined_types_.insert(make_pair(name, type));
  return true;
}

bool AidlTypenames::AddPreprocessedType(unique_ptr<AidlDefinedType> type) {
  const string name = type->GetCanonicalName();
  if (preprocessed_types_.find(name) != preprocessed_types_.end()) {
    return false;
  }
  preprocessed_types_.insert(make_pair(name, std::move(type)));
  return true;
}

bool AidlTypenames::IsBuiltinTypename(const string& type_name) {
  return kBuiltinTypes.find(type_name) != kBuiltinTypes.end() ||
      kJavaLikeTypeToAidlType.find(type_name) != kJavaLikeTypeToAidlType.end();
}

const AidlDefinedType* AidlTypenames::TryGetDefinedType(const string& type_name) const {
  // Do the exact match first.
  auto found_def = defined_types_.find(type_name);
  if (found_def != defined_types_.end()) {
    return found_def->second;
  }

  auto found_prep = preprocessed_types_.find(type_name);
  if (found_prep != preprocessed_types_.end()) {
    return found_prep->second.get();
  }

  // Then match with the class name. Defined types has higher priority than
  // types from the preprocessed file.
  for (auto it = defined_types_.begin(); it != defined_types_.end(); it++) {
    if (it->second->GetName() == type_name) {
      return it->second;
    }
  }

  for (auto it = preprocessed_types_.begin(); it != preprocessed_types_.end(); it++) {
    if (it->second->GetName() == type_name) {
      return it->second.get();
    }
  }

  return nullptr;
}

pair<string, bool> AidlTypenames::ResolveTypename(const string& type_name) const {
  if (IsBuiltinTypename(type_name)) {
    auto found = kJavaLikeTypeToAidlType.find(type_name);
    if (found != kJavaLikeTypeToAidlType.end()) {
      return make_pair(found->second, true);
    }
    return make_pair(type_name, true);
  }
  const AidlDefinedType* defined_type = TryGetDefinedType(type_name);
  if (defined_type != nullptr) {
    return make_pair(defined_type->GetCanonicalName(), true);
  } else {
    return make_pair(type_name, false);
  }
}

// Only T[], List, Map, and Parcelable can be an out parameter.
bool AidlTypenames::CanBeOutParameter(const AidlTypeSpecifier& type) const {
  const string& name = type.GetName();
  if (IsBuiltinTypename(name)) {
    return type.IsArray() || type.GetName() == "List" || type.GetName() == "Map";
  }
  const AidlDefinedType* t = TryGetDefinedType(type.GetName());
  CHECK(t != nullptr) << "Unrecognized type: '" << type.GetName() << "'";
  return t->AsParcelable() != nullptr;
}

}  // namespace aidl
}  // namespace android

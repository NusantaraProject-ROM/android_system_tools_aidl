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
  return builtin_types_.find(type_name) != builtin_types_.end();
}

const AidlDefinedType* AidlTypenames::TryGetDefinedType(const string& type_name) {
  // Do the exact match first.
  if (defined_types_.find(type_name) != defined_types_.end()) {
    return defined_types_[type_name];
  }

  if (preprocessed_types_.find(type_name) != preprocessed_types_.end()) {
    return preprocessed_types_[type_name].get();
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

pair<string, bool> AidlTypenames::ResolveTypename(const string& type_name) {
  if (IsBuiltinTypename(type_name)) {
    return make_pair(type_name, true);
  }
  const AidlDefinedType* defined_type = TryGetDefinedType(type_name);
  if (defined_type != nullptr) {
    return make_pair(defined_type->GetCanonicalName(), true);
  } else {
    return make_pair(type_name, false);
  }
}

}  // namespace aidl
}  // namespace android

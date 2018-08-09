/*
 * Copyright (C) 2018, The Android Open Source Project *
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

#include "aidl.h"
#include "aidl_language.h"
#include "import_resolver.h"
#include "options.h"
#include "type_java.h"

#include <map>
#include <string>
#include <vector>

namespace android {
namespace aidl {

using std::map;
using std::set;
using std::string;
using std::vector;

static bool have_compatible_annotations(const AidlAnnotatable& older,
                                        const AidlAnnotatable& newer) {
  if (older.GetAnnotations() != newer.GetAnnotations()) {
    const string from = older.ToString().empty() ? "(empty)" : older.ToString();
    const string to = newer.ToString().empty() ? "(empty)" : newer.ToString();
    AIDL_ERROR(newer) << "Changed annotations: " << from << " to " << to;
    return false;
  }
  return true;
}

static bool are_compatible_types(const AidlTypeSpecifier& older, const AidlTypeSpecifier& newer) {
  bool compatible = true;
  if (older.ToString() != newer.ToString()) {
    AIDL_ERROR(newer) << "Type changed: " << older.ToString() << " to " << newer.ToString() << ".";
    compatible = false;
  }
  compatible &= have_compatible_annotations(older, newer);
  return compatible;
}

static bool are_compatible_interfaces(const AidlInterface& older, const AidlInterface& newer) {
  bool compatible = true;
  compatible &= have_compatible_annotations(older, newer);

  map<string, AidlMethod*> new_methods;
  for (const auto& m : newer.AsInterface()->GetMethods()) {
    new_methods.emplace(m->Signature(), m.get());
  }

  for (const auto& old_m : older.AsInterface()->GetMethods()) {
    const auto found = new_methods.find(old_m->Signature());
    if (found == new_methods.end()) {
      AIDL_ERROR(old_m) << "Removed method: " << older.GetCanonicalName() << "."
                        << old_m->Signature();
      compatible = false;
      continue;
    }

    // Compare IDs to detect method reordering. IDs are assigned by their
    // textual order, so if there is an ID mismatch, that means reordering
    // has happened.
    const auto new_m = found->second;
    if (old_m->GetId() != new_m->GetId()) {
      AIDL_ERROR(new_m) << "Transaction ID changed: " << older.GetCanonicalName() << "."
                        << old_m->Signature() << " is changed from " << old_m->GetId() << " to "
                        << new_m->GetId() << ".";
      compatible = false;
    }

    compatible &= are_compatible_types(old_m->GetType(), new_m->GetType());

    const auto& old_args = old_m->GetArguments();
    const auto& new_args = new_m->GetArguments();
    // this is guaranteed because arguments are part of AidlMethod::Signature()
    CHECK(old_args.size() == new_args.size());
    for (size_t i = 0; i < old_args.size(); i++) {
      const AidlArgument& old_a = *(old_args.at(i));
      const AidlArgument& new_a = *(new_args.at(i));
      compatible &= are_compatible_types(old_a.GetType(), new_a.GetType());

      if (old_a.GetDirection() != new_a.GetDirection()) {
        AIDL_ERROR(new_m) << "Direction changed: " << old_a.GetDirectionSpecifier() << " to "
                          << new_a.GetDirectionSpecifier() << ".";
        compatible = false;
      }
    }
  }
  return compatible;
}

static bool are_compatible_parcelables(const AidlStructuredParcelable& older,
                                       const AidlStructuredParcelable& newer) {
  const auto& old_fields = older.GetFields();
  const auto& new_fields = newer.GetFields();
  if (old_fields.size() > new_fields.size()) {
    // you can add new fields only at the end
    AIDL_ERROR(newer) << "Number of fields in " << older.GetCanonicalName() << " is reduced from "
                      << old_fields.size() << " to " << new_fields.size() << ".";
    return false;
  }

  bool compatible = true;
  for (size_t i = 0; i < old_fields.size(); i++) {
    compatible &= are_compatible_types(old_fields.at(i)->GetType(), new_fields.at(i)->GetType());

    // Note: unlike method argument names, field name change is an incompatible
    // change, otherwise, we can't detect
    // parcelable Point {int x; int y;} -> parcelable Point {int y; int x;}
    if (old_fields.at(i)->GetName() != new_fields.at(i)->GetName()) {
      AIDL_ERROR(newer) << "Renamed field: " << old_fields.at(i)->GetName() << " to "
                        << new_fields.at(i)->GetName() << ".";
      compatible = false;
    }
  }
  return compatible;
}

bool check_api(const Options& options, const IoDelegate& io_delegate) {
  CHECK(options.IsStructured());
  CHECK(options.InputFiles().size() == 2) << "--checkapi requires two inputs "
                                          << "but got " << options.InputFiles().size();

  java::JavaTypeNamespace old_ns;
  old_ns.Init();
  const string old_input = options.InputFiles().at(0);
  vector<AidlDefinedType*> old_types;
  if (internals::load_and_validate_aidl(old_input, options, io_delegate, &old_ns, &old_types,
                                        nullptr /* imported_files */) != AidlError::OK) {
    AIDL_ERROR(old_input) << "Failed to read.";
    return false;
  }

  java::JavaTypeNamespace new_ns;
  new_ns.Init();
  const string new_input = options.InputFiles().at(1);
  vector<AidlDefinedType*> new_types;
  if (internals::load_and_validate_aidl(new_input, options, io_delegate, &new_ns, &new_types,
                                        nullptr /* imported_files */) != AidlError::OK) {
    AIDL_FATAL(new_input) << "Failed to read.";
    return false;
  }

  map<string, AidlDefinedType*> new_map;
  for (const auto t : new_types) {
    new_map.emplace(t->GetCanonicalName(), t);
  }

  bool compatible = true;
  for (const auto old_type : old_types) {
    const auto found = new_map.find(old_type->GetCanonicalName());
    if (found == new_map.end()) {
      AIDL_ERROR(old_type) << "Removed type: " << old_type->GetCanonicalName();
      compatible = false;
      continue;
    }
    const auto new_type = found->second;

    const bool old_is_iface = old_type->AsInterface() != nullptr;
    const bool new_is_iface = new_type->AsInterface() != nullptr;
    if (old_is_iface != new_is_iface) {
      AIDL_ERROR(new_type) << "Type mismatch: " << old_type->GetCanonicalName()
                           << " is changed from " << old_type->GetPreprocessDeclarationName()
                           << " to " << new_type->GetPreprocessDeclarationName();
      compatible = false;
      continue;
    }

    if (old_is_iface) {
      compatible &=
          are_compatible_interfaces(*(old_type->AsInterface()), *(new_type->AsInterface()));
    } else {
      CHECK(old_type->AsStructuredParcelable() != nullptr)
          << "Parcelable" << old_type->GetCanonicalName() << " is not structured. ";
      CHECK(new_type->AsStructuredParcelable() != nullptr)
          << "Parcelable" << new_type->GetCanonicalName() << " is not structured. ";
      compatible &= are_compatible_parcelables(*(old_type->AsStructuredParcelable()),
                                               *(new_type->AsStructuredParcelable()));
    }
  }

  return compatible;
}

}  // namespace aidl
}  // namespace android

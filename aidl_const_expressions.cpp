/*
 * Copyright (C) 2019, The Android Open Source Project
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

#include "aidl.h"
#include "aidl_language.h"
#include "logging.h"

#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <memory>

#include <android-base/parsedouble.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

using android::base::Join;
using std::string;
using std::unique_ptr;
using std::vector;

static bool isValidLiteralChar(char c) {
  return !(c <= 0x1f ||  // control characters are < 0x20
           c >= 0x7f ||  // DEL is 0x7f
           c == '\\');   // Disallow backslashes for future proofing.
}

AidlConstantValue* AidlConstantValue::Boolean(const AidlLocation& location, bool value) {
  return new AidlConstantValue(location, Type::BOOLEAN, value ? "true" : "false");
}

AidlConstantValue* AidlConstantValue::Character(const AidlLocation& location, char value) {
  if (!isValidLiteralChar(value)) {
    AIDL_ERROR(location) << "Invalid character literal " << value;
    return new AidlConstantValue(location, Type::ERROR, "");
  }
  return new AidlConstantValue(location, Type::CHARACTER, std::string("'") + value + "'");
}

AidlConstantValue* AidlConstantValue::Floating(const AidlLocation& location,
                                               const std::string& value) {
  return new AidlConstantValue(location, Type::FLOATING, value);
}

AidlConstantValue* AidlConstantValue::Hex(const AidlLocation& location, const std::string& value) {
  return new AidlConstantValue(location, Type::HEXIDECIMAL, value);
}

AidlConstantValue* AidlConstantValue::Integral(const AidlLocation& location,
                                               const std::string& value) {
  return new AidlConstantValue(location, Type::INTEGRAL, value);
}

AidlConstantValue* AidlConstantValue::Array(
    const AidlLocation& location, std::vector<std::unique_ptr<AidlConstantValue>>* values) {
  return new AidlConstantValue(location, Type::ARRAY, values);
}

AidlConstantValue* AidlConstantValue::String(const AidlLocation& location,
                                             const std::string& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isValidLiteralChar(value[i])) {
      AIDL_ERROR(location) << "Found invalid character at index " << i << " in string constant '"
                           << value << "'";
      return new AidlConstantValue(location, Type::ERROR, "");
    }
  }

  return new AidlConstantValue(location, Type::STRING, value);
}

bool AidlConstantValue::CheckValid() const {
  // error always logged during creation
  return type_ != AidlConstantValue::Type::ERROR;
}

static string TrimIfSuffix(const string& str, const string& suffix) {
  if (str.size() > suffix.size() &&
      0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix)) {
    return str.substr(0, str.size() - suffix.size());
  }
  return str;
}

string AidlConstantValue::As(const AidlTypeSpecifier& type,
                             const ConstantValueDecorator& decorator) const {
  if (type.IsGeneric()) {
    AIDL_ERROR(type) << "Generic type cannot be specified with a constant literal.";
    return "";
  }

  const std::string& type_string = type.GetName();

  if ((type_ == Type::ARRAY) != type.IsArray()) {
    goto mismatch_error;
  }

  switch (type_) {
    case AidlConstantValue::Type::ARRAY: {
      vector<string> raw_values;
      raw_values.reserve(values_.size());

      bool success = true;
      for (const auto& value : values_) {
        const AidlTypeSpecifier& array_base = type.ArrayBase();
        const std::string raw_value = value->As(array_base, decorator);

        success &= !raw_value.empty();
        raw_values.push_back(decorator(array_base, raw_value));
      }
      if (!success) {
        AIDL_ERROR(this) << "Default value must be a literal array of " << type_string << ".";
        return "";
      }
      return decorator(type, "{" + Join(raw_values, ", ") + "}");
    }
    case AidlConstantValue::Type::BOOLEAN:
      if (type_string == "boolean") return decorator(type, value_);
      goto mismatch_error;
    case AidlConstantValue::Type::CHARACTER:
      if (type_string == "char") return decorator(type, value_);
      goto mismatch_error;
    case AidlConstantValue::Type::FLOATING: {
      bool is_float_literal = value_.back() == 'f';
      const std::string raw_value = TrimIfSuffix(value_, "f");

      if (type_string == "double") {
        double parsed_value;
        if (!android::base::ParseDouble(raw_value, &parsed_value)) goto parse_error;
        return decorator(type, std::to_string(parsed_value));
      }
      if (is_float_literal && type_string == "float") {
        float parsed_value;
        if (!android::base::ParseFloat(raw_value, &parsed_value)) goto parse_error;
        return decorator(type, std::to_string(parsed_value) + "f");
      }
      goto mismatch_error;
    }
    case AidlConstantValue::Type::HEXIDECIMAL:
      // For historical reasons, a hexidecimal int needs to have the specified bits interpreted
      // as the signed type, so the other types are made consistent with it.
      if (type_string == "byte") {
        uint8_t unsigned_value;
        if (!android::base::ParseUint<uint8_t>(value_, &unsigned_value)) goto parse_error;
        return decorator(type, std::to_string((int8_t)unsigned_value));
      }
      if (type_string == "int") {
        uint32_t unsigned_value;
        if (!android::base::ParseUint<uint32_t>(value_, &unsigned_value)) goto parse_error;
        return decorator(type, std::to_string((int32_t)unsigned_value));
      }
      if (type_string == "long") {
        uint64_t unsigned_value;
        if (!android::base::ParseUint<uint64_t>(value_, &unsigned_value)) goto parse_error;
        return decorator(type, std::to_string((int64_t)unsigned_value));
      }
      goto mismatch_error;
    case AidlConstantValue::Type::INTEGRAL:
      if (type_string == "byte") {
        if (!android::base::ParseInt<int8_t>(value_, nullptr)) goto parse_error;
        return decorator(type, value_);
      }
      if (type_string == "int") {
        if (!android::base::ParseInt<int32_t>(value_, nullptr)) goto parse_error;
        return decorator(type, value_);
      }
      if (type_string == "long") {
        if (!android::base::ParseInt<int64_t>(value_, nullptr)) goto parse_error;
        return decorator(type, value_);
      }
      goto mismatch_error;
    case AidlConstantValue::Type::STRING:
      if (type_string == "String") return decorator(type, value_);
      goto mismatch_error;
    default:
      AIDL_FATAL(this) << "Unrecognized constant value type";
  }

mismatch_error:
  AIDL_ERROR(this) << "Expecting type " << type_string << " but constant is " << ToString(type_);
  return "";
parse_error:
  AIDL_ERROR(this) << "Could not parse " << value_ << " as " << type_string;
  return "";
}

string AidlConstantValue::ToString(Type type) {
  switch (type) {
    case Type::ARRAY:
      return "a literal array";
    case Type::BOOLEAN:
      return "a literal boolean";
    case Type::CHARACTER:
      return "a literal char";
    case Type::FLOATING:
      return "a floating-point literal";
    case Type::HEXIDECIMAL:
      return "a hexidecimal literal";
    case Type::INTEGRAL:
      return "an integral literal";
    case Type::STRING:
      return "a literal string";
    case Type::ERROR:
      LOG(FATAL) << "aidl internal error: error type failed to halt program";
      return "";
    default:
      LOG(FATAL) << "aidl internal error: unknown constant type: " << static_cast<int>(type);
      return "";  // not reached
  }
}

AidlConstantValue::AidlConstantValue(const AidlLocation& location, Type type,
                                     std::vector<std::unique_ptr<AidlConstantValue>>* values)
    : AidlNode(location), type_(type), values_(std::move(*values)) {}

AidlConstantValue::AidlConstantValue(const AidlLocation& location, Type type,
                                     const std::string& checked_value)
    : AidlNode(location), type_(type), value_(checked_value) {
  CHECK(!value_.empty() || type_ == Type::ERROR);
  CHECK(type_ != Type::ARRAY);
}

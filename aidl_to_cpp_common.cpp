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

#include "aidl_to_cpp_common.h"

#include "os.h"

namespace android {
namespace aidl {
namespace cpp {

string ClassName(const AidlDefinedType& defined_type, ClassNames type) {
  string c_name = defined_type.GetName();

  if (c_name.length() >= 2 && c_name[0] == 'I' && isupper(c_name[1])) c_name = c_name.substr(1);

  switch (type) {
    case ClassNames::CLIENT:
      c_name = "Bp" + c_name;
      break;
    case ClassNames::SERVER:
      c_name = "Bn" + c_name;
      break;
    case ClassNames::INTERFACE:
      c_name = "I" + c_name;
      break;
    case ClassNames::DEFAULT_IMPL:
      c_name = "I" + c_name + "Default";
      break;
    case ClassNames::BASE:
      break;
  }
  return c_name;
}

std::string HeaderFile(const AidlDefinedType& defined_type, ClassNames class_type,
                       bool use_os_sep) {
  std::string file_path = defined_type.GetPackage();
  for (char& c : file_path) {
    if (c == '.') {
      c = (use_os_sep) ? OS_PATH_SEPARATOR : '/';
    }
  }
  if (!file_path.empty()) {
    file_path += (use_os_sep) ? OS_PATH_SEPARATOR : '/';
  }
  file_path += ClassName(defined_type, class_type);
  file_path += ".h";

  return file_path;
}

void EnterNamespace(CodeWriter& out, const AidlDefinedType& defined_type) {
  const std::vector<std::string> packages = defined_type.GetSplitPackage();
  for (const std::string& package : packages) {
    out << "namespace " << package << " {\n";
  }
}
void LeaveNamespace(CodeWriter& out, const AidlDefinedType& defined_type) {
  const std::vector<std::string> packages = defined_type.GetSplitPackage();
  for (auto it = packages.rbegin(); it != packages.rend(); ++it) {
    out << "}  // namespace " << *it << "\n";
  }
}

string BuildVarName(const AidlArgument& a) {
  string prefix = "out_";
  if (a.GetDirection() & AidlArgument::IN_DIR) {
    prefix = "in_";
  }
  return prefix + a.GetName();
}

}  // namespace cpp
}  // namespace aidl
}  // namespace android

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

#include "generate_ndk.h"

#include "aidl_language.h"
#include "aidl_to_cpp_common.h"

#include <android-base/logging.h>

namespace android {
namespace aidl {
namespace ndk {

using namespace internals;
using cpp::ClassNames;

void GenerateNdkInterface(const string& output_file, const Options& options,
                          const AidlTypenames& types, const AidlInterface& defined_type,
                          const IoDelegate& io_delegate) {
  // TODO(b/112664205): generate constant values

  const string i_header =
      options.OutputHeaderDir() + HeaderFile(defined_type, ClassNames::INTERFACE);
  unique_ptr<CodeWriter> i_writer(io_delegate.GetCodeWriter(i_header));
  GenerateInterfaceHeader(*i_writer, types, defined_type, options);
  CHECK(i_writer->Close());

  const string bp_header = options.OutputHeaderDir() + HeaderFile(defined_type, ClassNames::CLIENT);
  unique_ptr<CodeWriter> bp_writer(io_delegate.GetCodeWriter(bp_header));
  GenerateClientHeader(*bp_writer, types, defined_type, options);
  CHECK(bp_writer->Close());

  const string bn_header = options.OutputHeaderDir() + HeaderFile(defined_type, ClassNames::SERVER);
  unique_ptr<CodeWriter> bn_writer(io_delegate.GetCodeWriter(bn_header));
  GenerateServerHeader(*bn_writer, types, defined_type, options);
  CHECK(bn_writer->Close());

  unique_ptr<CodeWriter> source_writer = io_delegate.GetCodeWriter(output_file);
  GenerateSource(*source_writer, types, defined_type, options);
  CHECK(source_writer->Close());
}

void GenerateNdkParcel(const string& output_file, const Options& options,
                       const AidlTypenames& types, const AidlStructuredParcelable& defined_type,
                       const IoDelegate& io_delegate) {
  const string header_path = options.OutputHeaderDir() + HeaderFile(defined_type, ClassNames::BASE);
  unique_ptr<CodeWriter> header_writer(io_delegate.GetCodeWriter(header_path));
  GenerateParcelHeader(*header_writer, types, defined_type, options);
  CHECK(header_writer->Close());

  const string bp_header = options.OutputHeaderDir() + HeaderFile(defined_type, ClassNames::CLIENT);
  unique_ptr<CodeWriter> bp_writer(io_delegate.GetCodeWriter(bp_header));
  *bp_writer << "#error TODO(b/111362593) defined_types do not have bp classes\n";
  CHECK(bp_writer->Close());

  const string bn_header = options.OutputHeaderDir() + HeaderFile(defined_type, ClassNames::SERVER);
  unique_ptr<CodeWriter> bn_writer(io_delegate.GetCodeWriter(bn_header));
  *bn_writer << "#error TODO(b/111362593) defined_types do not have bn classes\n";
  CHECK(bn_writer->Close());

  unique_ptr<CodeWriter> source_writer = io_delegate.GetCodeWriter(output_file);
  GenerateParcelSource(*source_writer, types, defined_type, options);
  CHECK(source_writer->Close());
}

void GenerateNdk(const string& output_file, const Options& options, const AidlTypenames& types,
                 const AidlDefinedType& defined_type, const IoDelegate& io_delegate) {
  const AidlStructuredParcelable* parcelable = defined_type.AsStructuredParcelable();
  if (parcelable != nullptr) {
    GenerateNdkParcel(output_file, options, types, *parcelable, io_delegate);
    return;
  }

  const AidlInterface* interface = defined_type.AsInterface();
  if (interface != nullptr) {
    GenerateNdkInterface(output_file, options, types, *interface, io_delegate);
    return;
  }

  CHECK(false) << "Unrecognized type sent for cpp generation.";
}
namespace internals {

void EnterNdkNamespace(CodeWriter& out, const AidlDefinedType& defined_type) {
  out << "namespace aidl {\n";
  cpp::EnterNamespace(out, defined_type);
}
void LeaveNdkNamespace(CodeWriter& out, const AidlDefinedType& defined_type) {
  cpp::LeaveNamespace(out, defined_type);
  out << "}  // namespace aidl\n";
}

void GenerateSource(CodeWriter& out, const AidlTypenames& types, const AidlInterface& defined_type,
                    const Options& options) {
  out << "#include \"" << HeaderFile(defined_type, ClassNames::CLIENT, false /*use_os_sep*/)
      << "\"\n";
  out << "#include \"" << HeaderFile(defined_type, ClassNames::SERVER, false /*use_os_sep*/)
      << "\"\n";
  out << "#include \"" << HeaderFile(defined_type, ClassNames::INTERFACE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  GenerateClientSource(out, types, defined_type, options);
  GenerateServerSource(out, types, defined_type, options);
  GenerateInterfaceSource(out, types, defined_type, options);
  LeaveNdkNamespace(out, defined_type);
}
void GenerateClientSource(CodeWriter& out, const AidlTypenames& types,
                          const AidlInterface& defined_type, const Options& options) {
  const std::string clazz = ClassName(defined_type, ClassNames::CLIENT);

  out << "// Source for " << clazz << "\n";
  out << clazz << "::" << clazz
      << "(const ::android::AutoAIBinder& binder) : BpCInterface(binder) {}\n";
  out << clazz << "::~" << clazz << "() {}\n";

  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateServerSource(CodeWriter& out, const AidlTypenames& types,
                          const AidlInterface& defined_type, const Options& options) {
  const std::string clazz = ClassName(defined_type, ClassNames::SERVER);

  out << "// Source for " << clazz << "\n";
  out << clazz << "::" << clazz << "() {}\n";
  out << clazz << "::~" << clazz << "() {}\n";

  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateInterfaceSource(CodeWriter& out, const AidlTypenames& types,
                             const AidlInterface& defined_type, const Options& options) {
  const std::string clazz = ClassName(defined_type, ClassNames::INTERFACE);

  out << "// Source for " << clazz << "\n";
  out << "const char* " << clazz << "::descriptor = \"" << defined_type.GetCanonicalName()
      << "\";\n";
  out << clazz << "::" << clazz << "() {}\n";
  out << clazz << "::~" << clazz << "() {}\n";

  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateClientHeader(CodeWriter& out, const AidlTypenames& types,
                          const AidlInterface& defined_type, const Options& options) {
  const std::string clazz = ClassName(defined_type, ClassNames::CLIENT);

  out << "#pragma once\n\n";
  out << "#include \"" << HeaderFile(defined_type, ClassNames::INTERFACE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  out << "#include <android/binder_ibinder.h>\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  out << "class " << clazz << " : public ::android::BpCInterface<"
      << ClassName(defined_type, ClassNames::INTERFACE) << "> {\n";
  out << "public:\n";
  out.Indent();
  out << clazz << "(const ::android::AutoAIBinder& binder);\n";
  out << "virtual ~" << clazz << "();\n";
  out.Dedent();
  out << "private:\n";
  out.Indent();
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateServerHeader(CodeWriter& out, const AidlTypenames& types,
                          const AidlInterface& defined_type, const Options& options) {
  const std::string clazz = ClassName(defined_type, ClassNames::SERVER);

  out << "#pragma once\n\n";
  out << "#include \"" << HeaderFile(defined_type, ClassNames::INTERFACE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  out << "#include <android/binder_ibinder.h>\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  out << "class " << clazz << " : public ::android::BnCInterface<"
      << ClassName(defined_type, ClassNames::INTERFACE) << "> {\n";
  out << "public:\n";
  out.Indent();
  out << clazz << "();\n";
  out << "virtual ~" << clazz << "();\n";
  out.Dedent();
  out << "private:\n";
  out.Indent();
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateInterfaceHeader(CodeWriter& out, const AidlTypenames& types,
                             const AidlInterface& defined_type, const Options& options) {
  const std::string clazz = ClassName(defined_type, ClassNames::INTERFACE);

  out << "#pragma once\n\n";
  out << "#include <android/binder_interface_utils.h>\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  // TODO(b/112664205): still need to create an equivalent of IInterface which implements methods
  // like 'ping'
  out << "class " << clazz << " : public ::android::ICInterface {\n";
  out << "public:\n";
  out.Indent();
  out << "static const char* descriptor;\n";
  out << clazz << "();\n";
  out << "virtual ~" << clazz << "();\n";
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateParcelHeader(CodeWriter& out, const AidlTypenames& types,
                          const AidlStructuredParcelable& defined_type, const Options& options) {
  out << "#pragma once\n";
  EnterNdkNamespace(out, defined_type);
  LeaveNdkNamespace(out, defined_type);
  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
void GenerateParcelSource(CodeWriter& out, const AidlTypenames& types,
                          const AidlStructuredParcelable& defined_type, const Options& options) {
  out << "#include \"" << HeaderFile(defined_type, ClassNames::BASE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  LeaveNdkNamespace(out, defined_type);
  (void)types;    // TODO(b/112664205)
  (void)options;  // TODO(b/112664205)
}
}  // namespace internals
}  // namespace ndk
}  // namespace aidl
}  // namespace android

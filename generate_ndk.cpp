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
#include "aidl_to_ndk.h"

#include <android-base/logging.h>

namespace android {
namespace aidl {
namespace ndk {

using namespace internals;
using cpp::ClassNames;

void GenerateNdkInterface(const string& output_file, const Options& options,
                          const AidlTypenames& types, const AidlInterface& defined_type,
                          const IoDelegate& io_delegate) {
  const string i_header =
      options.OutputHeaderDir() + NdkHeaderFile(defined_type, ClassNames::INTERFACE);
  unique_ptr<CodeWriter> i_writer(io_delegate.GetCodeWriter(i_header));
  GenerateInterfaceHeader(*i_writer, types, defined_type, options);
  CHECK(i_writer->Close());

  const string bp_header =
      options.OutputHeaderDir() + NdkHeaderFile(defined_type, ClassNames::CLIENT);
  unique_ptr<CodeWriter> bp_writer(io_delegate.GetCodeWriter(bp_header));
  GenerateClientHeader(*bp_writer, types, defined_type, options);
  CHECK(bp_writer->Close());

  const string bn_header =
      options.OutputHeaderDir() + NdkHeaderFile(defined_type, ClassNames::SERVER);
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
  const string header_path =
      options.OutputHeaderDir() + NdkHeaderFile(defined_type, ClassNames::BASE);
  unique_ptr<CodeWriter> header_writer(io_delegate.GetCodeWriter(header_path));
  GenerateParcelHeader(*header_writer, types, defined_type, options);
  CHECK(header_writer->Close());

  const string bp_header =
      options.OutputHeaderDir() + NdkHeaderFile(defined_type, ClassNames::CLIENT);
  unique_ptr<CodeWriter> bp_writer(io_delegate.GetCodeWriter(bp_header));
  *bp_writer << "#error TODO(b/111362593) defined_types do not have bp classes\n";
  CHECK(bp_writer->Close());

  const string bn_header =
      options.OutputHeaderDir() + NdkHeaderFile(defined_type, ClassNames::SERVER);
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

static void StatusCheckGoto(CodeWriter& out) {
  out << "if (_aidl_ret_status != STATUS_OK) goto _aidl_error;\n\n";
}
static void StatusCheckBreak(CodeWriter& out) {
  out << "if (_aidl_ret_status != STATUS_OK) break;\n\n";
}
static void StatusCheckReturn(CodeWriter& out) {
  out << "if (_aidl_ret_status != STATUS_OK) return _aidl_ret_status;\n\n";
}

static void GenerateHeaderIncludes(CodeWriter& out, const AidlTypenames& types,
                                   const AidlDefinedType& defined_type) {
  out << "#include <android/binder_parcel_utils.h>\n";

  types.IterateTypes([&](const AidlDefinedType& other_defined_type) {
    if (&other_defined_type == &defined_type) return;

    if (other_defined_type.AsInterface() != nullptr) {
      out << "#include <"
          << NdkHeaderFile(other_defined_type, ClassNames::INTERFACE, false /*use_os_sep*/)
          << ">\n";
    } else if (other_defined_type.AsStructuredParcelable() != nullptr) {
      out << "#include <"
          << NdkHeaderFile(other_defined_type, ClassNames::BASE, false /*use_os_sep*/) << ">\n";
    } else if (other_defined_type.AsParcelable() != nullptr) {
      out << "#include \"" << other_defined_type.AsParcelable()->GetCppHeader() << "\"\n";
    } else {
      AIDL_FATAL(defined_type) << "Unrecognized type.";
    }
  });
}
static void GenerateSourceIncludes(CodeWriter& out, const AidlTypenames& types,
                                   const AidlDefinedType& /*defined_type*/) {
  types.IterateTypes([&](const AidlDefinedType& a_defined_type) {
    if (a_defined_type.AsInterface() != nullptr) {
      out << "#include <" << NdkHeaderFile(a_defined_type, ClassNames::CLIENT, false /*use_os_sep*/)
          << ">\n";
      out << "#include <" << NdkHeaderFile(a_defined_type, ClassNames::SERVER, false /*use_os_sep*/)
          << ">\n";
      out << "#include <"
          << NdkHeaderFile(a_defined_type, ClassNames::INTERFACE, false /*use_os_sep*/) << ">\n";
    }
  });
}

static void GenerateConstantDeclarations(CodeWriter& out, const AidlInterface& interface) {
  for (const auto& constant : interface.GetConstantDeclarations()) {
    const AidlConstantValue& value = constant->GetValue();
    if (value.GetType() == AidlConstantValue::Type::STRING) {
      out << "static const char* " << constant->GetName() << ";\n";
    }
  }
  out << "\n";

  bool hasIntegralConstant = false;
  for (const auto& constant : interface.GetConstantDeclarations()) {
    const AidlConstantValue& value = constant->GetValue();
    if (value.GetType() == AidlConstantValue::Type::HEXIDECIMAL ||
        value.GetType() == AidlConstantValue::Type::INTEGRAL) {
      hasIntegralConstant = true;
      break;
    }
  }

  if (hasIntegralConstant) {
    out << "enum : int32_t {\n";
    out.Indent();
    for (const auto& constant : interface.GetConstantDeclarations()) {
      const AidlConstantValue& value = constant->GetValue();
      if (value.GetType() == AidlConstantValue::Type::HEXIDECIMAL ||
          value.GetType() == AidlConstantValue::Type::INTEGRAL) {
        out << constant->GetName() << " = " << constant->ValueString(AidlConstantValueDecorator)
            << ",\n";
      }
    }
    out.Dedent();
    out << "};\n";
  }
}
static void GenerateConstantDefinitions(CodeWriter& out, const AidlInterface& interface) {
  const std::string clazz = ClassName(interface, ClassNames::INTERFACE);

  for (const auto& constant : interface.GetConstantDeclarations()) {
    const AidlConstantValue& value = constant->GetValue();
    if (value.GetType() == AidlConstantValue::Type::STRING) {
      out << "const char* " << clazz << "::" << constant->GetName() << " = "
          << constant->ValueString(AidlConstantValueDecorator) << ";\n";
    }
  }
}

void GenerateSource(CodeWriter& out, const AidlTypenames& types, const AidlInterface& defined_type,
                    const Options& options) {
  GenerateSourceIncludes(out, types, defined_type);
  out << "\n";

  EnterNdkNamespace(out, defined_type);
  GenerateClassSource(out, types, defined_type, options);
  GenerateClientSource(out, types, defined_type, options);
  GenerateServerSource(out, types, defined_type, options);
  GenerateInterfaceSource(out, types, defined_type, options);
  LeaveNdkNamespace(out, defined_type);
}

static std::string DataClassFor(const AidlInterface& defined_type) {
  return "AidlClassData_" + ClassName(defined_type, ClassNames::INTERFACE);
}
static std::string MethodId(const AidlMethod& m) {
  return "(FIRST_CALL_TRANSACTION + " + std::to_string(m.GetId()) + " /*" + m.GetName() + "*/)";
}

static void GenerateClientMethodDefinition(CodeWriter& out, const AidlTypenames& types,
                                           const AidlInterface& defined_type,
                                           const AidlMethod& method) {
  const std::string clazz = ClassName(defined_type, ClassNames::CLIENT);

  out << NdkMethodDecl(types, method, clazz) << " {\n";
  out.Indent();
  out << "::ndk::ScopedAParcel _aidl_in;\n";
  out << "::ndk::ScopedAParcel _aidl_out;\n";
  out << "binder_status_t _aidl_ret_status = STATUS_OK;\n";
  out << "::ndk::ScopedAStatus _aidl_status;\n";
  out << "\n";

  out << "_aidl_ret_status = AIBinder_prepareTransaction(asBinder().get(), _aidl_in.getR());\n";
  StatusCheckGoto(out);

  for (const AidlArgument* arg : method.GetInArguments()) {
    out << "_aidl_ret_status = ";
    std::string prefix = arg->IsOut() ? "*" : "";
    WriteToParcelFor(
        {out, types, arg->GetType(), "_aidl_in.get()", prefix + cpp::BuildVarName(*arg)});
    out << ";\n";
    StatusCheckGoto(out);
  }
  out << "_aidl_ret_status = AIBinder_transact(\n";
  out.Indent();
  out << "asBinder().get(),\n";
  out << MethodId(method) << ",\n";
  out << "_aidl_in.getR(),\n";
  out << "_aidl_out.getR(),\n";
  out << (method.IsOneway() ? "FLAG_ONEWAY" : "0") << ");\n";
  out.Dedent();
  StatusCheckGoto(out);

  if (!method.IsOneway()) {
    out << "_aidl_ret_status = AParcel_readStatusHeader(_aidl_out.get(), _aidl_status.getR());\n";
    StatusCheckGoto(out);

    out << "if (!AStatus_isOk(_aidl_status.get())) return _aidl_status;\n\n";
  }

  for (const AidlArgument* arg : method.GetOutArguments()) {
    out << "_aidl_ret_status = ";
    ReadFromParcelFor({out, types, arg->GetType(), "_aidl_out.get()", cpp::BuildVarName(*arg)});
    out << ";\n";
    StatusCheckGoto(out);
  }

  if (method.GetType().GetName() != "void") {
    out << "_aidl_ret_status = ";
    ReadFromParcelFor({out, types, method.GetType(), "_aidl_out.get()", "_aidl_return"});
    out << ";\n";
    StatusCheckGoto(out);
  }

  out << "_aidl_error:\n";
  out << "_aidl_status.set(AStatus_fromStatus(_aidl_ret_status));\n";
  out << "return _aidl_status;\n";
  out.Dedent();
  out << "}\n";
}

static void GenerateServerCaseDefinition(CodeWriter& out, const AidlTypenames& types,
                                         const AidlInterface& /*defined_type*/,
                                         const AidlMethod& method) {
  out << "case " << MethodId(method) << ": {\n";
  out.Indent();
  for (const auto& arg : method.GetArguments()) {
    out << NdkNameOf(types, arg->GetType(), StorageMode::STACK) << " " << cpp::BuildVarName(*arg)
        << ";\n";
  }
  if (method.GetType().GetName() != "void") {
    out << NdkNameOf(types, method.GetType(), StorageMode::STACK) << " _aidl_return;\n";
  }
  out << "\n";

  for (const AidlArgument* arg : method.GetInArguments()) {
    out << "_aidl_ret_status = ";
    ReadFromParcelFor({out, types, arg->GetType(), "_aidl_in", "&" + cpp::BuildVarName(*arg)});
    out << ";\n";
    StatusCheckBreak(out);
  }

  out << "::ndk::ScopedAStatus _aidl_status = _aidl_impl->" << method.GetName() << "("
      << NdkCallListFor(method) << ");\n";

  if (method.IsOneway()) {
    // For a oneway transaction, the kernel will have already returned a result. This is for the
    // in-process case when a oneway transaction is parceled/unparceled in the same process.
    out << "_aidl_ret_status = STATUS_OK;\n";
  } else {
    out << "_aidl_ret_status = AParcel_writeStatusHeader(_aidl_out, _aidl_status.get());\n";
    StatusCheckBreak(out);

    out << "if (!AStatus_isOk(_aidl_status.get())) break;\n\n";

    for (const AidlArgument* arg : method.GetOutArguments()) {
      out << "_aidl_ret_status = ";
      WriteToParcelFor({out, types, arg->GetType(), "_aidl_out", cpp::BuildVarName(*arg)});
      out << ";\n";
      StatusCheckBreak(out);
    }
    if (method.GetType().GetName() != "void") {
      out << "_aidl_ret_status = ";
      WriteToParcelFor({out, types, method.GetType(), "_aidl_out", "_aidl_return"});
      out << ";\n";
      StatusCheckBreak(out);
    }
  }

  out << "break;\n";
  out.Dedent();
  out << "}\n";
}

void GenerateClassSource(CodeWriter& out, const AidlTypenames& types,
                         const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::INTERFACE);
  const std::string bn_clazz = ClassName(defined_type, ClassNames::SERVER);
  const std::string data_clazz = DataClassFor(defined_type);
  const std::string on_create = data_clazz + "_onCreate";
  const std::string on_destroy = data_clazz + "_onDestory";
  const std::string on_transact = data_clazz + "_onTransact";

  out << "struct " << data_clazz << " {\n";
  out.Indent();
  out << "static AIBinder_Class* clazz;\n";
  out << "std::shared_ptr<" << bn_clazz << "> instance;\n";
  out.Dedent();
  out << "};\n\n";

  out << "static void* " << on_create << "(void* args) {\n";
  out.Indent();
  out << data_clazz << "* data = new " << data_clazz << "{static_cast<" << bn_clazz
      << "*>(args)->ref<" << bn_clazz << ">()};\n";
  out << "return static_cast<void*>(data);\n";
  out.Dedent();
  out << "};\n\n";

  out << "static void " << on_destroy << "(void* userData) {\n";
  out.Indent();
  out << "delete static_cast<" << data_clazz << "*>(userData);\n";
  out.Dedent();
  out << "};\n\n";

  out << "static binder_status_t " << on_transact
      << "(AIBinder* _aidl_binder, transaction_code_t _aidl_code, const AParcel* _aidl_in, "
         "AParcel* _aidl_out) {\n";
  out.Indent();
  out << "(void)_aidl_in;\n";
  out << "(void)_aidl_out;\n";
  out << "binder_status_t _aidl_ret_status = STATUS_UNKNOWN_TRANSACTION;\n";
  if (!defined_type.GetMethods().empty()) {
    out << "std::shared_ptr<" << bn_clazz << "> _aidl_impl = static_cast<" << data_clazz
        << "*>(AIBinder_getUserData(_aidl_binder))->instance;\n";
    out << "switch (_aidl_code) {\n";
    out.Indent();
    for (const auto& method : defined_type.GetMethods()) {
      GenerateServerCaseDefinition(out, types, defined_type, *method);
    }
    out.Dedent();
    out << "}\n";
  } else {
    out << "(void)_aidl_binder;\n";
    out << "(void)_aidl_code;\n";
  }
  out << "return _aidl_ret_status;\n";
  out.Dedent();
  out << "};\n\n";

  out << "AIBinder_Class* " << data_clazz << "::clazz = AIBinder_Class_define(" << clazz
      << "::descriptor, " << on_create << ", " << on_destroy << ", " << on_transact << ");\n\n";
}
void GenerateClientSource(CodeWriter& out, const AidlTypenames& types,
                          const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::CLIENT);
  const std::string data_clazz = DataClassFor(defined_type);

  out << "// Source for " << clazz << "\n";
  out << "std::shared_ptr<" << clazz << "> " << clazz
      << "::associate(const ::ndk::SpAIBinder& binder) {\n";
  out.Indent();
  out << "if (!AIBinder_associateClass(binder.get(), " << data_clazz
      << "::clazz)) { return nullptr; }\n";
  out << "return (new " << clazz << "(binder))->ref<" << clazz << ">();\n";
  out.Dedent();
  out << "}\n\n";

  out << clazz << "::" << clazz << "(const ::ndk::SpAIBinder& binder) : BpCInterface(binder) {}\n";
  out << clazz << "::~" << clazz << "() {}\n";
  out << "\n";
  for (const auto& method : defined_type.GetMethods()) {
    GenerateClientMethodDefinition(out, types, defined_type, *method);
  }
}
void GenerateServerSource(CodeWriter& out, const AidlTypenames& /*types*/,
                          const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::SERVER);
  const std::string data_clazz = DataClassFor(defined_type);

  out << "// Source for " << clazz << "\n";
  out << clazz << "::" << clazz << "() {}\n";
  out << clazz << "::~" << clazz << "() {}\n";

  out << "::ndk::SpAIBinder " << clazz << "::createBinder() {\n";
  out.Indent();
  out << "AIBinder* binder = AIBinder_new(" << data_clazz
      << "::clazz, static_cast<void*>(this));\n";
  out << "return ::ndk::SpAIBinder(binder);\n";
  out.Dedent();
  out << "}\n";
}
void GenerateInterfaceSource(CodeWriter& out, const AidlTypenames& /*types*/,
                             const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::INTERFACE);
  const std::string data_clazz = DataClassFor(defined_type);

  out << "// Source for " << clazz << "\n";
  out << "const char* " << clazz << "::descriptor = \"" << defined_type.GetCanonicalName()
      << "\";\n";
  out << clazz << "::" << clazz << "() {}\n";
  out << clazz << "::~" << clazz << "() {}\n";
  out << "\n";
  GenerateConstantDefinitions(out, defined_type);
  out << "\n";

  out << "binder_status_t " << clazz << "::writeToParcel(AParcel* parcel, const std::shared_ptr<"
      << clazz << ">& instance) {\n";
  out.Indent();
  out << "return AParcel_writeStrongBinder(parcel, instance ? instance->asBinder().get() : "
         "nullptr);\n";
  out.Dedent();
  out << "}\n";

  out << "binder_status_t " << clazz << "::readFromParcel(const AParcel* parcel, std::shared_ptr<"
      << clazz << ">* instance) {\n";
  out.Indent();
  out << "::ndk::SpAIBinder binder;\n";
  out << "binder_status_t status = AParcel_readNullableStrongBinder(parcel, binder.getR());\n";
  out << "if (status != STATUS_OK) return status;\n";
  out << data_clazz << "* data = static_cast<" << data_clazz
      << "*>(AIBinder_getUserData(binder.get()));\n";
  out << "if (data) {\n";
  out.Indent();
  out << "*instance = data->instance;\n";
  out.Dedent();
  out << "} else {\n";
  out.Indent();
  out << "*instance = " << NdkFullClassName(defined_type, ClassNames::CLIENT)
      << "::associate(binder);\n";
  out.Dedent();
  out << "}\n";
  out << "return STATUS_OK;\n";
  out.Dedent();
  out << "}\n";
}
void GenerateClientHeader(CodeWriter& out, const AidlTypenames& types,
                          const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::CLIENT);

  out << "#pragma once\n\n";
  out << "#include \"" << NdkHeaderFile(defined_type, ClassNames::INTERFACE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  out << "#include <android/binder_ibinder.h>\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  out << "class " << clazz << " : public ::ndk::BpCInterface<"
      << ClassName(defined_type, ClassNames::INTERFACE) << "> {\n";
  out << "public:\n";
  out.Indent();
  out << "static std::shared_ptr<" << clazz << "> associate(const ::ndk::SpAIBinder& binder);\n";
  out << "virtual ~" << clazz << "();\n";
  out << "\n";
  for (const auto& method : defined_type.GetMethods()) {
    out << NdkMethodDecl(types, *method) << " override;\n";
  }
  out.Dedent();
  out << "private:\n";
  out.Indent();
  out << clazz << "(const ::ndk::SpAIBinder& binder);\n";
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
}
void GenerateServerHeader(CodeWriter& out, const AidlTypenames& /*types*/,
                          const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::SERVER);

  out << "#pragma once\n\n";
  out << "#include \"" << NdkHeaderFile(defined_type, ClassNames::INTERFACE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  out << "#include <android/binder_ibinder.h>\n";
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  out << "class " << clazz << " : public ::ndk::BnCInterface<"
      << ClassName(defined_type, ClassNames::INTERFACE) << "> {\n";
  out << "public:\n";
  out.Indent();
  out << clazz << "();\n";
  out << "virtual ~" << clazz << "();\n";
  out.Dedent();
  out << "protected:\n";
  out.Indent();
  out << "::ndk::SpAIBinder createBinder() override;\n";
  out.Dedent();
  out << "private:\n";
  out.Indent();
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
}
void GenerateInterfaceHeader(CodeWriter& out, const AidlTypenames& types,
                             const AidlInterface& defined_type, const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::INTERFACE);

  out << "#pragma once\n\n";
  out << "#include <android/binder_interface_utils.h>\n";
  out << "\n";

  GenerateHeaderIncludes(out, types, defined_type);
  out << "\n";

  EnterNdkNamespace(out, defined_type);
  out << "class " << clazz << " : public ::ndk::ICInterface {\n";
  out << "public:\n";
  out.Indent();
  out << "static AIBinder_Class* clazz;\n";
  out << "static const char* descriptor;\n";
  out << clazz << "();\n";
  out << "virtual ~" << clazz << "();\n";
  out << "\n";
  GenerateConstantDeclarations(out, defined_type);
  out << "\n";
  out << "static binder_status_t writeToParcel(AParcel* parcel, const std::shared_ptr<" << clazz
      << ">& instance);";
  out << "static binder_status_t readFromParcel(const AParcel* parcel, std::shared_ptr<" << clazz
      << ">* instance);";
  out << "\n";
  for (const auto& method : defined_type.GetMethods()) {
    out << "virtual " << NdkMethodDecl(types, *method) << " = 0;\n";
  }
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
}
void GenerateParcelHeader(CodeWriter& out, const AidlTypenames& types,
                          const AidlStructuredParcelable& defined_type,
                          const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::BASE);

  out << "#pragma once\n";
  out << "#include <android/binder_interface_utils.h>\n";
  out << "\n";

  GenerateHeaderIncludes(out, types, defined_type);

  EnterNdkNamespace(out, defined_type);
  out << "class " << clazz << " {\n";
  out << "public:\n";
  out.Indent();
  out << "static const char* descriptor;\n";
  out << "\n";
  for (const auto& variable : defined_type.GetFields()) {
    out << NdkNameOf(types, variable->GetType(), StorageMode::STACK) << " " << variable->GetName();
    if (variable->GetDefaultValue()) {
      out << " = " << variable->ValueString(AidlConstantValueDecorator);
    }
    out << ";\n";
  }
  out << "\n";
  out << "binder_status_t readFromParcel(const AParcel* parcel);\n";
  out << "binder_status_t writeToParcel(AParcel* parcel) const;\n";
  out.Dedent();
  out << "};\n";
  LeaveNdkNamespace(out, defined_type);
}
void GenerateParcelSource(CodeWriter& out, const AidlTypenames& types,
                          const AidlStructuredParcelable& defined_type,
                          const Options& /*options*/) {
  const std::string clazz = ClassName(defined_type, ClassNames::BASE);

  out << "#include \"" << NdkHeaderFile(defined_type, ClassNames::BASE, false /*use_os_sep*/)
      << "\"\n";
  out << "\n";
  GenerateSourceIncludes(out, types, defined_type);
  out << "\n";
  EnterNdkNamespace(out, defined_type);
  out << "const char* " << clazz << "::descriptor = \"" << defined_type.GetCanonicalName()
      << "\";\n";
  out << "\n";

  out << "binder_status_t " << clazz << "::readFromParcel(const AParcel* parcel) {\n";
  out.Indent();
  out << "std::string _aidl_descriptor;\n";
  out << "binder_status_t _aidl_ret_status;\n";

  out << "int32_t _aidl_null;\n";
  out << "_aidl_ret_status = AParcel_readInt32(parcel, &_aidl_null);\n";
  StatusCheckReturn(out);

  // TODO(b/117281836)
  out << "if (_aidl_null == 0) return STATUS_UNEXPECTED_NULL;\n\n";

  for (const auto& variable : defined_type.GetFields()) {
    out << "_aidl_ret_status = ";
    ReadFromParcelFor({out, types, variable->GetType(), "parcel", "&" + variable->GetName()});
    out << ";\n";
    StatusCheckReturn(out);
  }
  out << "return _aidl_ret_status;\n";
  out.Dedent();
  out << "}\n";

  out << "binder_status_t " << clazz << "::writeToParcel(AParcel* parcel) const {\n";
  out.Indent();
  out << "binder_status_t _aidl_ret_status;\n";

  // non-null
  out << "_aidl_ret_status = AParcel_writeInt32(parcel, 1);\n";
  StatusCheckReturn(out);

  for (const auto& variable : defined_type.GetFields()) {
    out << "_aidl_ret_status = ";
    WriteToParcelFor({out, types, variable->GetType(), "parcel", variable->GetName()});
    out << ";\n";
    StatusCheckReturn(out);
  }
  out << "return _aidl_ret_status;\n";
  out.Dedent();
  out << "}\n";
  out << "\n";
  LeaveNdkNamespace(out, defined_type);
}
}  // namespace internals
}  // namespace ndk
}  // namespace aidl
}  // namespace android

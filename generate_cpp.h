/*
 * Copyright (C) 2015, The Android Open Source Project
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

#include <memory>
#include <string>

#include "aidl_language.h"
#include "aidl_to_cpp.h"
#include "ast_cpp.h"
#include "options.h"
#include "type_cpp.h"

namespace android {
namespace aidl {
namespace cpp {

bool GenerateCpp(const string& output_file, const Options& options, const cpp::TypeNamespace& types,
                 const AidlDefinedType& parsed_doc, const IoDelegate& io_delegate);

// These roughly correspond to the various class names in the C++ hierarchy:
enum class ClassNames {
  BASE,          // Foo (not a real class, but useful in some circumstances).
  CLIENT,        // BpFoo
  SERVER,        // BnFoo
  INTERFACE,     // IFoo
  DEFAULT_IMPL,  // IFooDefault
};

// Generate the relative path to a header file.  If |use_os_sep| we'll use the
// operating system specific path separator rather than C++'s expected '/' when
// including headers.
std::string HeaderFile(const AidlDefinedType& defined_type, ClassNames class_type,
                       bool use_os_sep = true);

namespace internals {
std::unique_ptr<Document> BuildClientSource(const TypeNamespace& types,
                                            const AidlInterface& parsed_doc,
                                            const Options& options);
std::unique_ptr<Document> BuildServerSource(const TypeNamespace& types,
                                            const AidlInterface& parsed_doc,
                                            const Options& options);
std::unique_ptr<Document> BuildInterfaceSource(const TypeNamespace& types,
                                               const AidlInterface& parsed_doc,
                                               const Options& options);
std::unique_ptr<Document> BuildClientHeader(const TypeNamespace& types,
                                            const AidlInterface& parsed_doc,
                                            const Options& options);
std::unique_ptr<Document> BuildServerHeader(const TypeNamespace& types,
                                            const AidlInterface& parsed_doc,
                                            const Options& options);
std::unique_ptr<Document> BuildInterfaceHeader(const TypeNamespace& types,
                                               const AidlInterface& parsed_doc,
                                               const Options& options);

std::unique_ptr<Document> BuildParcelHeader(const TypeNamespace& types,
                                            const AidlStructuredParcelable& parsed_doc,
                                            const Options& options);
std::unique_ptr<Document> BuildParcelSource(const TypeNamespace& types,
                                            const AidlStructuredParcelable& parsed_doc,
                                            const Options& options);
}
}  // namespace cpp
}  // namespace aidl
}  // namespace android

/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "aidl.h"
#include "fake_io_delegate.h"
#include "options.h"

#include <iostream>

#ifdef FUZZ_LOG
constexpr bool kFuzzLog = true;
#else
constexpr bool kFuzzLog = false;
#endif

using android::aidl::test::FakeIoDelegate;

void fuzz(uint8_t options, const std::string& content) {
  uint8_t lang = options & 0x3;
  std::string langOpt;
  switch (lang) {
    case 1:
      langOpt = "cpp";
      break;
    case 2:
      langOpt = "ndk";
      break;
    case 3:
      langOpt = "java";
      break;
    default:
      return;
  }

  // TODO: fuzz multiple files
  // TODO: fuzz arguments
  FakeIoDelegate io;
  io.SetFileContents("a/path/Foo.aidl", content);

  std::vector<std::string> args;
  args.emplace_back("aidl");
  args.emplace_back("--lang=" + langOpt);
  args.emplace_back("-b");
  args.emplace_back("-I .");
  args.emplace_back("-o out");
  // corresponding items also in aidl_parser_fuzzer.dict
  args.emplace_back("a/path/Foo.aidl");

  if (kFuzzLog) {
    std::cout << "lang: " << langOpt << " content: " << content << std::endl;
  }

  int ret = android::aidl::compile_aidl(Options::From(args), io);
  if (ret != 0) return;

  if (kFuzzLog) {
    for (const std::string& f : io.ListOutputFiles()) {
      std::string output;
      if (io.GetWrittenContents(f, &output)) {
        std::cout << "OUTPUT " << f << ": " << std::endl;
        std::cout << output << std::endl;
      }
    }
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size <= 1) return 0;  // no use

  uint8_t options = *data;
  data++;
  size--;

  std::string content(reinterpret_cast<const char*>(data), size);
  fuzz(options, content);

  return 0;
}

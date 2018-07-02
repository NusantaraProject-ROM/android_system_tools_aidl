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

#include "code_writer.h"

#include <fstream>
#include <iostream>
#include <stdarg.h>
#include <vector>

#include <android-base/stringprintf.h>

namespace android {
namespace aidl {

std::string CodeWriter::ApplyIndent(const std::string& str) {
  std::string output;
  if (!start_of_line_ || str == "\n") {
    output = str;
  } else {
    output = std::string(indent_level_ * 2, ' ') + str;
  }
  start_of_line_ = !output.empty() && output.back() == '\n';
  return output;
}

bool CodeWriter::Write(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string formatted;
  android::base::StringAppendV(&formatted, format, ap);
  va_end(ap);

  // extract lines. empty line is preserved.
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < formatted.size()) {
    size_t line_end = formatted.find('\n', pos);
    if (line_end != std::string::npos) {
      lines.push_back(formatted.substr(pos, (line_end - pos) + 1));
      pos = line_end + 1;
    } else {
      lines.push_back(formatted.substr(pos));
      break;
    }
  }

  std::string indented;
  for (auto line : lines) {
    indented.append(ApplyIndent(line));
  }
  return Output(indented);
}

class StringCodeWriter : public CodeWriter {
 public:
  explicit StringCodeWriter(std::string* output_buffer) : output_(output_buffer) {}
  virtual ~StringCodeWriter() = default;

  bool Output(const std::string& str) override {
    output_->append(str);
    return true;
  }

  bool Close() override { return true; }

 private:
  std::string* output_;
};  // class StringCodeWriter

class FileCodeWriter : public CodeWriter {
 public:
  explicit FileCodeWriter(const std::string& filename) : cout_(std::cout) {
    if (filename == "-") {
      to_stdout_ = true;
    } else {
      to_stdout_ = false;
      fileout_.open(filename, std::ofstream::out |
                    std::ofstream::binary);
      if (fileout_.fail()) {
        std::cerr << "unable to open " << filename << " for write" << std::endl;
      }
    }
  }

  bool Output(const std::string& str) override {
    if (to_stdout_) {
      cout_ << str;
    } else {
      fileout_ << str;
    }
    return TestSuccess();
  }

  bool Close() override {
    if (!to_stdout_) {
      fileout_.close();
    }
    return TestSuccess();
  }

  bool TestSuccess() const {
    return to_stdout_ ? true : !fileout_.fail();
  }

 private:
  std::ostream& cout_;
  std::ofstream fileout_;
  bool to_stdout_;
};  // class StringCodeWriter

CodeWriterPtr GetFileWriter(const std::string& output_file) {
  return CodeWriterPtr(new FileCodeWriter(output_file));
}

CodeWriterPtr GetStringWriter(std::string* output_buffer) {
  return CodeWriterPtr(new StringCodeWriter(output_buffer));
}

}  // namespace aidl
}  // namespace android

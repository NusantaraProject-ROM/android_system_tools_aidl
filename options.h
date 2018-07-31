/*
 * Copyright (C) 2015, The Android Open Source Project *
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

#ifndef AIDL_OPTIONS_H_
#define AIDL_OPTIONS_H_

#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

namespace android {
namespace aidl {

using std::string;
using std::vector;

class Options final {
 public:
  enum class Language { UNSPECIFIED, JAVA, CPP };

  enum class Task { UNSPECIFIED, COMPILE, PREPROCESS, DUMPAPI };

  Options(int argc, const char* const argv[], Language default_lang = Language::UNSPECIFIED);

  // Contain no references to unstructured data types (such as a parcelable that is
  // implemented in Java). These interfaces aren't inherently stable but they have the
  // capacity to be stabilized.
  bool IsStructured() const { return structured_; }

  Language TargetLanguage() const { return language_; }

  Task GetTask() const { return task_; }

  const vector<string>& ImportPaths() const { return import_paths_; }

  const vector<string>& PreprocessedFiles() const { return preprocessed_files_; }

  string DependencyFile() const {
    if (auto_dep_file_) {
      return output_file_ + ".d";
    }
    return dependency_file_;
  }

  bool AutoDepFile() const { return auto_dep_file_; }

  bool GenTraces() const { return gen_traces_; }

  bool GenTransactionNames() const { return gen_transaction_names_; }

  bool DependencyFileNinja() const { return dependency_file_ninja_; }

  const vector<string>& InputFiles() const { return input_files_; }

  // Path to the output file. This is used only when there is only one
  // output file for the invocation. When there are multiple outputs
  // (e.g. compile multiple AIDL files), output files are created under
  // OutputDir().
  const string& OutputFile() const { return output_file_; }

  // Path to the directory where output file(s) will be generated under.
  const string& OutputDir() const { return output_dir_; }

  // Path to the directory where header file(s) will be generated under.
  // Only used when TargetLanguage() == Language::CPP
  const string& OutputHeaderDir() const { return output_header_dir_; }

  bool FailOnParcelable() const { return fail_on_parcelable_; }

  bool Ok() const { return error_message_.str().empty(); }

  string GetErrorMessage() const { return error_message_.str(); }

  string GetUsage() const;

  // The following are for testability, but cannot be influenced on the command line.
  // Threshold of interface methods to enable outlining of onTransact cases.
  size_t onTransact_outline_threshold_{275u};
  // Number of cases to _not_ outline, if outlining is enabled.
  size_t onTransact_non_outline_count_{275u};

 private:
  Options() = default;

  const string myname_;
  bool structured_ = false;
  Language language_ = Language::UNSPECIFIED;
  Task task_ = Task::COMPILE;
  vector<string> import_paths_;
  vector<string> preprocessed_files_;
  string dependency_file_;
  bool gen_traces_ = false;
  bool gen_transaction_names_ = false;
  bool dependency_file_ninja_ = false;
  string output_dir_;
  string output_header_dir_;
  bool fail_on_parcelable_ = false;
  bool auto_dep_file_ = false;
  vector<string> input_files_;
  string output_file_;
  std::ostringstream error_message_;

  FRIEND_TEST(EndToEndTest, IExampleInterface);
  FRIEND_TEST(EndToEndTest, IExampleInterface_WithTransactionNames);
  FRIEND_TEST(EndToEndTest, IExampleInterface_WithTrace);
  FRIEND_TEST(EndToEndTest, IExampleInterface_Outlining);
  FRIEND_TEST(AidlTest, FailOnParcelable);
  FRIEND_TEST(AidlTest, WritePreprocessedFile);
  FRIEND_TEST(AidlTest, WritesCorrectDependencyFile);
  FRIEND_TEST(AidlTest, WritesCorrectDependencyFileNinja);
  FRIEND_TEST(AidlTest, WritesTrivialDependencyFileForParcelable);
  FRIEND_TEST(AidlTest, ApiDump);
  FRIEND_TEST(AidlTest, CheckNumGenericTypeSecifier);
};

}  // namespace android
}  // namespace aidl

#endif // AIDL_OPTIONS_H_

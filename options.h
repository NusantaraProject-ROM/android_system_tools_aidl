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

#include <memory>
#include <string>
#include <vector>

#include <android-base/macros.h>
#include <gtest/gtest_prod.h>

namespace android {
namespace aidl {

class Options {
 public:
  virtual ~Options() = default;

  // flag arguments
  std::vector<std::string> import_paths_;
  std::vector<std::string> preprocessed_files_;
  std::string dep_file_name_;
  bool gen_traces_{false};
  bool dep_file_ninja_{false};

  // positional arguments
  std::string input_file_name_;
  std::string output_file_name_;

 protected:
  enum class Result {
    CONSUMED,
    UNHANDLED,
    ERROR,
  };

  // parses all flag arguments
  virtual Result ParseFlag(int index, const std::string& arg);

  // print all flag arguments
  virtual void FlagUsage();
};

// This object represents the parsed options to the Java generating aidl.
class JavaOptions final : public Options {
 public:
  enum {
      COMPILE_AIDL_TO_JAVA,
      PREPROCESS_AIDL,
  };

  // Parses the command line and returns a non-null pointer to an JavaOptions
  // object on success.
  // Prints the usage statement on failure.
  static std::unique_ptr<JavaOptions> Parse(int argc, const char* const* argv);

  std::string DependencyFilePath() const;
  bool DependencyFileNinja() const { return dep_file_ninja_; }
  bool ShouldGenGetTransactionName() const { return gen_transaction_names_; }

  Result ParseFlag(int index, const std::string& arg) override;
  void FlagUsage() override;

  // flag arguments
  int task{COMPILE_AIDL_TO_JAVA};
  bool fail_on_parcelable_{false};
  bool auto_dep_file_{false};
  bool gen_transaction_names_{false};  // for Binder#getTransactionName
  std::vector<std::string> files_to_preprocess_;

  // positional arguments
  std::string output_base_folder_;

  // The following are for testability, but cannot be influenced on the command line.
  // Threshold of interface methods to enable outlining of onTransact cases.
  size_t onTransact_outline_threshold_{275u};
  // Number of cases to _not_ outline, if outlining is enabled.
  size_t onTransact_non_outline_count_{275u};

 private:
  JavaOptions() = default;

  std::unique_ptr<JavaOptions> Usage();

  FRIEND_TEST(EndToEndTest, IExampleInterface);
  FRIEND_TEST(EndToEndTest, IExampleInterface_WithTransactionNames);
  FRIEND_TEST(EndToEndTest, IExampleInterface_WithTrace);
  FRIEND_TEST(EndToEndTest, IExampleInterface_Outlining);
  FRIEND_TEST(AidlTest, FailOnParcelable);
  FRIEND_TEST(AidlTest, WritePreprocessedFile);
  FRIEND_TEST(AidlTest, WritesCorrectDependencyFile);
  FRIEND_TEST(AidlTest, WritesCorrectDependencyFileNinja);
  FRIEND_TEST(AidlTest, WritesTrivialDependencyFileForParcelable);

  DISALLOW_COPY_AND_ASSIGN(JavaOptions);
};

class CppOptions final : public Options {
 public:
  // Parses the command line and returns a non-null pointer to an CppOptions
  // object on success.
  // Prints the usage statement on failure.
  static std::unique_ptr<CppOptions> Parse(int argc, const char* const* argv);

  std::string InputFileName() const { return input_file_name_; }
  std::string OutputHeaderDir() const { return output_header_dir_; }
  std::string OutputCppFilePath() const { return output_file_name_; }

  std::vector<std::string> ImportPaths() const { return import_paths_; }
  std::string DependencyFilePath() const { return dep_file_name_; }
  bool DependencyFileNinja() const { return dep_file_ninja_; }
  bool ShouldGenTraces() const { return gen_traces_; }

 private:
  CppOptions() = default;

  std::unique_ptr<CppOptions> Usage();

  std::string output_header_dir_;

  FRIEND_TEST(CppOptionsTests, ParsesCompileCpp);
  FRIEND_TEST(CppOptionsTests, ParsesCompileCppNinja);
  DISALLOW_COPY_AND_ASSIGN(CppOptions);
};

bool EndsWith(const std::string& str, const std::string& suffix);
bool ReplaceSuffix(const std::string& old_suffix,
                   const std::string& new_suffix,
                   std::string* str);

}  // namespace android
}  // namespace aidl

#endif // AIDL_OPTIONS_H_

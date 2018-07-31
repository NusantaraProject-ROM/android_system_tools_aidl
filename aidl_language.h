#ifndef AIDL_AIDL_LANGUAGE_H_
#define AIDL_AIDL_LANGUAGE_H_

#include "aidl_typenames.h"
#include "code_writer.h"
#include "io_delegate.h"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <android-base/macros.h>
#include <android-base/strings.h>

struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;

using android::aidl::CodeWriter;
using std::string;
using std::unique_ptr;
using std::vector;

class AidlToken {
 public:
  AidlToken(const std::string& text, const std::string& comments);

  const std::string& GetText() const { return text_; }
  const std::string& GetComments() const { return comments_; }

 private:
  std::string text_;
  std::string comments_;

  DISALLOW_COPY_AND_ASSIGN(AidlToken);
};

class AidlNode {
 public:
  AidlNode() = default;
  virtual ~AidlNode() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(AidlNode);
};

namespace android {
namespace aidl {

class ValidatableType;
class AidlTypenames;

}  // namespace aidl
}  // namespace android

class AidlAnnotation : public AidlNode {
 public:
  AidlAnnotation(const string& name, string& error);
  virtual ~AidlAnnotation() = default;
  const string& GetName() const { return name_; }
  string ToString() const { return "@" + name_; }

 private:
  const string name_;
};

class AidlAnnotatable : public AidlNode {
 public:
  AidlAnnotatable() = default;
  virtual ~AidlAnnotatable() = default;

  void Annotate(set<unique_ptr<AidlAnnotation>>&& annotations) {
    annotations_ = std::move(annotations);
  }
  bool IsNullable() const;
  bool IsUtf8() const;
  bool IsUtf8InCpp() const;
  std::string ToString() const;

 private:
  set<unique_ptr<AidlAnnotation>> annotations_;

  DISALLOW_COPY_AND_ASSIGN(AidlAnnotatable);
};

class AidlQualifiedName;

// AidlTypeSpecifier represents a reference to either a built-in type,
// a defined type, or a variant (e.g., array of generic) of a type.
class AidlTypeSpecifier final : public AidlAnnotatable {
 public:
  AidlTypeSpecifier(const string& unresolved_name, bool is_array,
                    vector<unique_ptr<AidlTypeSpecifier>>* type_params, unsigned line,
                    const string& comments);
  virtual ~AidlTypeSpecifier() = default;

  // Returns the full-qualified name of the base type.
  // int -> int
  // int[] -> int
  // List<String> -> List
  // IFoo -> foo.bar.IFoo (if IFoo is in package foo.bar)
  const string& GetName() const {
    if (IsResolved()) {
      return fully_qualified_name_;
    } else {
      return GetUnresolvedName();
    }
  }

  // Returns string representation of this type specifier.
  // This is GetBaseTypeName() + array modifieir or generic type parameters
  string ToString() const;

  std::string Signature() const;

  const string& GetUnresolvedName() const { return unresolved_name_; }

  const string& GetComments() const { return comments_; }

  bool IsResolved() const { return fully_qualified_name_ != ""; }

  bool IsArray() const { return is_array_; }

  bool IsGeneric() const { return type_params_ != nullptr; }

  unsigned GetLine() const { return line_; }

  const vector<unique_ptr<AidlTypeSpecifier>>& GetTypeParameters() const { return *type_params_; }

  // Resolve the base type name to a fully-qualified name. Return false if the
  // resolution fails.
  bool Resolve(android::aidl::AidlTypenames& typenames);

  bool CheckValid() const;

  void SetLanguageType(const android::aidl::ValidatableType* language_type) {
    language_type_ = language_type;
  }

  template<typename T>
  const T* GetLanguageType() const {
    return reinterpret_cast<const T*>(language_type_);
  }
 private:
  const string unresolved_name_;
  string fully_qualified_name_;
  const bool is_array_;
  const unique_ptr<vector<unique_ptr<AidlTypeSpecifier>>> type_params_;
  const unsigned line_;
  const string comments_;
  const android::aidl::ValidatableType* language_type_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(AidlTypeSpecifier);
};

class AidlConstantValue;
class AidlVariableDeclaration : public AidlNode {
 public:
  AidlVariableDeclaration(AidlTypeSpecifier* type, std::string name, unsigned line);
  AidlVariableDeclaration(AidlTypeSpecifier* type, std::string name, unsigned line,
                          AidlConstantValue* default_value);
  virtual ~AidlVariableDeclaration() = default;

  std::string GetName() const { return name_; }
  int GetLine() const { return line_; }
  const AidlTypeSpecifier& GetType() const { return *type_; }
  const AidlConstantValue* GetDefaultValue() const { return default_value_.get(); }

  AidlTypeSpecifier* GetMutableType() { return type_.get(); }

  bool CheckValid() const;
  std::string ToString() const;
  std::string Signature() const;

 private:
  std::unique_ptr<AidlTypeSpecifier> type_;
  std::string name_;
  unsigned line_;
  std::unique_ptr<AidlConstantValue> default_value_;

  DISALLOW_COPY_AND_ASSIGN(AidlVariableDeclaration);
};

class AidlArgument : public AidlVariableDeclaration {
 public:
  enum Direction { IN_DIR = 1, OUT_DIR = 2, INOUT_DIR = 3 };

  AidlArgument(AidlArgument::Direction direction, AidlTypeSpecifier* type, std::string name,
               unsigned line);
  AidlArgument(AidlTypeSpecifier* type, std::string name, unsigned line);
  virtual ~AidlArgument() = default;

  Direction GetDirection() const { return direction_; }
  bool IsOut() const { return direction_ & OUT_DIR; }
  bool IsIn() const { return direction_ & IN_DIR; }
  bool DirectionWasSpecified() const { return direction_specified_; }

  std::string ToString() const;
  std::string Signature() const;

 private:
  string GetDirectionSpecifier() const;
  Direction direction_;
  bool direction_specified_;

  DISALLOW_COPY_AND_ASSIGN(AidlArgument);
};

class AidlMethod;
class AidlConstantDeclaration;
class AidlMember : public AidlNode {
 public:
  AidlMember() = default;
  virtual ~AidlMember() = default;

  virtual AidlMethod* AsMethod() { return nullptr; }
  virtual AidlConstantDeclaration* AsConstantDeclaration() { return nullptr; }

 private:
  DISALLOW_COPY_AND_ASSIGN(AidlMember);
};

class AidlConstantValue : public AidlNode {
 public:
  enum class Type { ERROR, INTEGER, STRING };
  static string ToString(Type type);

  virtual ~AidlConstantValue() = default;

  static AidlConstantValue* LiteralInt(const int32_t value);
  // example: "0x4f"
  static AidlConstantValue* ParseHex(const std::string& value, unsigned line);
  // example: "\"asdf\""
  static AidlConstantValue* ParseString(const std::string& value, unsigned line);

  Type GetType() const { return type_; }
  string ToString() const;

 private:
  AidlConstantValue() = default;
  AidlConstantValue(Type type, const std::string& checked_value);

  const Type type_ = Type::ERROR;
  const std::string value_;

  DISALLOW_COPY_AND_ASSIGN(AidlConstantValue);
};

class AidlConstantDeclaration : public AidlMember {
 public:
  AidlConstantDeclaration(AidlTypeSpecifier* specifier, std::string name, AidlConstantValue* value,
                          unsigned line);
  virtual ~AidlConstantDeclaration() = default;

  const AidlTypeSpecifier& GetType() const { return *type_; }
  const std::string& GetName() const { return name_; }
  const AidlConstantValue& GetValue() const { return *value_; }
  bool CheckValid() const;

  AidlConstantDeclaration* AsConstantDeclaration() override { return this; }

 private:
  const unique_ptr<AidlTypeSpecifier> type_;
  const std::string name_;
  const unique_ptr<AidlConstantValue> value_;
  const unsigned line_;

  DISALLOW_COPY_AND_ASSIGN(AidlConstantDeclaration);
};

class AidlMethod : public AidlMember {
 public:
  AidlMethod(bool oneway, AidlTypeSpecifier* type, std::string name,
             std::vector<std::unique_ptr<AidlArgument>>* args, unsigned line,
             const std::string& comments);
  AidlMethod(bool oneway, AidlTypeSpecifier* type, std::string name,
             std::vector<std::unique_ptr<AidlArgument>>* args, unsigned line,
             const std::string& comments, int id);
  virtual ~AidlMethod() = default;

  AidlMethod* AsMethod() override { return this; }

  const std::string& GetComments() const { return comments_; }
  const AidlTypeSpecifier& GetType() const { return *type_; }
  AidlTypeSpecifier* GetMutableType() { return type_.get(); }
  bool IsOneway() const { return oneway_; }
  const std::string& GetName() const { return name_; }
  unsigned GetLine() const { return line_; }
  bool HasId() const { return has_id_; }
  int GetId() { return id_; }
  void SetId(unsigned id) { id_ = id; }

  const std::vector<std::unique_ptr<AidlArgument>>& GetArguments() const {
    return arguments_;
  }
  // An inout parameter will appear in both GetInArguments()
  // and GetOutArguments().  AidlMethod retains ownership of the argument
  // pointers returned in this way.
  const std::vector<const AidlArgument*>& GetInArguments() const {
    return in_arguments_;
  }
  const std::vector<const AidlArgument*>& GetOutArguments() const {
    return out_arguments_;
  }

  std::string Signature() const;

 private:
  bool oneway_;
  std::string comments_;
  std::unique_ptr<AidlTypeSpecifier> type_;
  std::string name_;
  unsigned line_;
  const std::vector<std::unique_ptr<AidlArgument>> arguments_;
  std::vector<const AidlArgument*> in_arguments_;
  std::vector<const AidlArgument*> out_arguments_;
  bool has_id_;
  int id_;

  DISALLOW_COPY_AND_ASSIGN(AidlMethod);
};

class AidlDefinedType;
class AidlInterface;
class AidlParcelable;
class AidlStructuredParcelable;
class AidlDocument : public AidlNode {
 public:
  AidlDocument() = default;
  virtual ~AidlDocument() = default;

  AidlDefinedType* ReleaseDefinedType();

  const std::vector<std::unique_ptr<AidlDefinedType>>& GetDefinedTypes() const {
    return defined_types_;
  }
  void AddDefinedType(AidlDefinedType* defined_type) {
    defined_types_.push_back(std::unique_ptr<AidlDefinedType>(defined_type));
  }

 private:
  std::vector<std::unique_ptr<AidlDefinedType>> defined_types_;

  DISALLOW_COPY_AND_ASSIGN(AidlDocument);
};

class AidlQualifiedName : public AidlNode {
 public:
  AidlQualifiedName(std::string term, std::string comments);
  virtual ~AidlQualifiedName() = default;

  const std::vector<std::string>& GetTerms() const { return terms_; }
  const std::string& GetComments() const { return comments_; }
  std::string GetDotName() const { return android::base::Join(terms_, '.'); }
  std::string GetColonName() const { return android::base::Join(terms_, "::"); }

  void AddTerm(const std::string& term);

 private:
  std::vector<std::string> terms_;
  std::string comments_;

  DISALLOW_COPY_AND_ASSIGN(AidlQualifiedName);
};

// AidlDefinedType represents either an interface or a parcelable that is
// defined in the source file.
class AidlDefinedType : public AidlAnnotatable {
 public:
  AidlDefinedType(std::string name, unsigned line,
                  const std::string& comments,
                  const std::vector<std::string>& package);
  virtual ~AidlDefinedType() = default;

  const std::string& GetName() const { return name_; };
  unsigned GetLine() const { return line_; }
  const std::string& GetComments() const { return comments_; }

  /* dot joined package, example: "android.package.foo" */
  std::string GetPackage() const;
  /* dot joined package and name, example: "android.package.foo.IBar" */
  std::string GetCanonicalName() const;
  const std::vector<std::string>& GetSplitPackage() const { return package_; }

  virtual std::string GetPreprocessDeclarationName() const = 0;

  virtual const AidlStructuredParcelable* AsStructuredParcelable() const { return nullptr; }
  virtual const AidlParcelable* AsParcelable() const { return nullptr; }
  virtual const AidlInterface* AsInterface() const { return nullptr; }

  AidlStructuredParcelable* AsStructuredParcelable() {
    return const_cast<AidlStructuredParcelable*>(
        const_cast<const AidlDefinedType*>(this)->AsStructuredParcelable());
  }
  AidlParcelable* AsParcelable() {
    return const_cast<AidlParcelable*>(const_cast<const AidlDefinedType*>(this)->AsParcelable());
  }
  AidlInterface* AsInterface() {
    return const_cast<AidlInterface*>(const_cast<const AidlDefinedType*>(this)->AsInterface());
  }

  const AidlParcelable* AsUnstructuredParcelable() const {
    if (this->AsStructuredParcelable() != nullptr) return nullptr;
    return this->AsParcelable();
  }
  AidlParcelable* AsUnstructuredParcelable() {
    return const_cast<AidlParcelable*>(
        const_cast<const AidlDefinedType*>(this)->AsUnstructuredParcelable());
  }

  void SetLanguageType(const android::aidl::ValidatableType* language_type) {
    language_type_ = language_type;
  }

  template <typename T>
  const T* GetLanguageType() const {
    return reinterpret_cast<const T*>(language_type_);
  }

  virtual void Write(CodeWriter* writer) const = 0;

 private:
  std::string name_;
  unsigned line_;
  std::string comments_;
  const android::aidl::ValidatableType* language_type_ = nullptr;
  const std::vector<std::string> package_;

  DISALLOW_COPY_AND_ASSIGN(AidlDefinedType);
};

class AidlParcelable : public AidlDefinedType {
 public:
  AidlParcelable(AidlQualifiedName* name, unsigned line,
                 const std::vector<std::string>& package,
                 const std::string& cpp_header = "");
  virtual ~AidlParcelable() = default;

  // C++ uses "::" instead of "." to refer to a inner class.
  std::string GetCppName() const { return name_->GetColonName(); }
  std::string GetCppHeader() const { return cpp_header_; }

  const AidlParcelable* AsParcelable() const override { return this; }
  std::string GetPreprocessDeclarationName() const override { return "parcelable"; }

  void Write(CodeWriter* writer) const override;

 private:
  std::unique_ptr<AidlQualifiedName> name_;
  std::string cpp_header_;

  DISALLOW_COPY_AND_ASSIGN(AidlParcelable);
};

class AidlStructuredParcelable : public AidlParcelable {
 public:
  AidlStructuredParcelable(AidlQualifiedName* name, unsigned line,
                           const std::vector<std::string>& package,
                           std::vector<std::unique_ptr<AidlVariableDeclaration>>* variables);

  const std::vector<std::unique_ptr<AidlVariableDeclaration>>& GetFields() const {
    return variables_;
  }

  const AidlStructuredParcelable* AsStructuredParcelable() const override { return this; }
  std::string GetPreprocessDeclarationName() const override { return "structured_parcelable"; }

  void Write(CodeWriter* writer) const override;

 private:
  const std::vector<std::unique_ptr<AidlVariableDeclaration>> variables_;

  DISALLOW_COPY_AND_ASSIGN(AidlStructuredParcelable);
};

class AidlInterface final : public AidlDefinedType {
 public:
  AidlInterface(const std::string& name, unsigned line,
                const std::string& comments, bool oneway_,
                std::vector<std::unique_ptr<AidlMember>>* members,
                const std::vector<std::string>& package);
  virtual ~AidlInterface() = default;

  bool IsOneway() const { return oneway_; }
  const std::vector<std::unique_ptr<AidlMethod>>& GetMethods() const
      { return methods_; }
  const std::vector<std::unique_ptr<AidlConstantDeclaration>>& GetConstantDeclarations() const {
    return constants_;
  }

  void SetGenerateTraces(bool generate_traces) {
    generate_traces_ = generate_traces;
  }

  bool ShouldGenerateTraces() const {
    return generate_traces_;
  }

  const AidlInterface* AsInterface() const override { return this; }
  std::string GetPreprocessDeclarationName() const override { return "interface"; }

  void Write(CodeWriter* writer) const override;

 private:
  bool oneway_;
  std::vector<std::unique_ptr<AidlMethod>> methods_;
  std::vector<std::unique_ptr<AidlConstantDeclaration>> constants_;

  bool generate_traces_ = false;

  DISALLOW_COPY_AND_ASSIGN(AidlInterface);
};

class AidlImport : public AidlNode {
 public:
  AidlImport(const std::string& from, const std::string& needed_class,
             unsigned line);
  virtual ~AidlImport() = default;

  const std::string& GetFileFrom() const { return from_; }
  const std::string& GetFilename() const { return filename_; }
  const std::string& GetNeededClass() const { return needed_class_; }
  unsigned GetLine() const { return line_; }
  const AidlDocument* GetAidlDocument() const {
    // can return nullptr when AidlDocument is not set.
    return imported_doc_.get();
  }

  void SetFilename(const std::string& filename) { filename_ = filename; }
  void SetAidlDocument(unique_ptr<AidlDocument>&& doc) { imported_doc_ = std::move(doc); }

 private:
  std::string from_;
  std::string filename_;
  std::string needed_class_;
  unsigned line_;
  unique_ptr<AidlDocument> imported_doc_;

  DISALLOW_COPY_AND_ASSIGN(AidlImport);
};

class Parser {
 public:
  explicit Parser(const android::aidl::IoDelegate& io_delegate,
                  android::aidl::AidlTypenames& typenames);
  ~Parser();

  // Parse contents of file |filename|.
  bool ParseFile(const std::string& filename);

  void AddError() { error_++; }

  const std::string& FileName() const { return filename_; }
  void* Scanner() const { return scanner_; }

  void SetDocument(AidlDocument* doc) { document_.reset(doc); };

  void AddImport(AidlQualifiedName* name, unsigned line);

  std::vector<std::string> Package() const;
  void SetPackage(AidlQualifiedName* name) { package_.reset(name); }

  AidlDocument* GetDocument() const { return document_.get(); }
  AidlDocument* ReleaseDocument() { return document_.release(); }
  const std::vector<std::unique_ptr<AidlImport>>& GetImports() {
    return imports_;
  }

  void ReleaseImports(std::vector<std::unique_ptr<AidlImport>>* ret) {
      *ret = std::move(imports_);
      imports_.clear();
  }

  android::aidl::AidlTypenames& GetTypenames() { return typenames_; }

  void DeferResolution(AidlTypeSpecifier* typespec) {
    unresolved_typespecs_.emplace_back(typespec);
  }

  bool Resolve();

 private:
  const android::aidl::IoDelegate& io_delegate_;
  int error_ = 0;
  std::string filename_;
  std::unique_ptr<AidlQualifiedName> package_;
  void* scanner_ = nullptr;
  std::unique_ptr<AidlDocument> document_;
  std::vector<std::unique_ptr<AidlImport>> imports_;
  std::unique_ptr<std::string> raw_buffer_;
  YY_BUFFER_STATE buffer_;
  android::aidl::AidlTypenames& typenames_;
  vector<AidlTypeSpecifier*> unresolved_typespecs_;

  DISALLOW_COPY_AND_ASSIGN(Parser);
};

#endif // AIDL_AIDL_LANGUAGE_H_

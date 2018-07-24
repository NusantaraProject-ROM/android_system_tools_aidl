#include "aidl_language.h"
#include "aidl_typenames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "aidl_language_y.h"
#include "logging.h"
#include "type_java.h"
#include "type_namespace.h"

#ifdef _WIN32
int isatty(int  fd)
{
    return (fd == 0);
}
#endif

using android::aidl::IoDelegate;
using android::base::Join;
using android::base::Split;
using std::cerr;
using std::endl;
using std::pair;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

void yylex_init(void **);
void yylex_destroy(void *);
void yyset_in(FILE *f, void *);
int yyparse(Parser*);
YY_BUFFER_STATE yy_scan_buffer(char *, size_t, void *);
void yy_delete_buffer(YY_BUFFER_STATE, void *);

AidlToken::AidlToken(const std::string& text, const std::string& comments)
    : text_(text),
      comments_(comments) {}

static const string kNullable("nullable");
static const string kUtf8("utf8");
static const string kUtf8InCpp("utf8InCpp");

static const set<string> kAnnotationNames{kNullable, kUtf8, kUtf8InCpp};

AidlAnnotation::AidlAnnotation(const string& name, string& error) : name_(name) {
  if (kAnnotationNames.find(name_) == kAnnotationNames.end()) {
    std::ostringstream stream;
    stream << "'" << name_ << "' is not a recognized annotation. ";
    stream << "It must be one of:";
    for (const string& kv : kAnnotationNames) {
      stream << " " << kv;
    }
    stream << ".";
    error = stream.str();
  }
}

static bool HasAnnotation(const set<unique_ptr<AidlAnnotation>>& annotations, const string& name) {
  for (const auto& a : annotations) {
    if (a->GetName() == name) {
      return true;
    }
  }
  return false;
}

bool AidlAnnotatable::IsNullable() const {
  return HasAnnotation(annotations_, kNullable);
}

bool AidlAnnotatable::IsUtf8() const {
  return HasAnnotation(annotations_, kUtf8);
}

bool AidlAnnotatable::IsUtf8InCpp() const {
  return HasAnnotation(annotations_, kUtf8InCpp);
}

string AidlAnnotatable::ToString() const {
  vector<string> ret;
  for (const auto& a : annotations_) {
    ret.emplace_back(a->ToString());
  }
  std::sort(ret.begin(), ret.end());
  return Join(ret, " ");
}

AidlTypeSpecifier::AidlTypeSpecifier(const string& unresolved_name, bool is_array,
                                     vector<unique_ptr<AidlTypeSpecifier>>* type_params,
                                     unsigned line, const string& comments)
    : unresolved_name_(unresolved_name),
      is_array_(is_array),
      type_params_(type_params),
      line_(line),
      comments_(comments) {}

string AidlTypeSpecifier::ToString() const {
  string ret = GetName();
  if (IsGeneric()) {
    vector<string> arg_names;
    for (const auto& ta : GetTypeParameters()) {
      arg_names.emplace_back(ta->ToString());
    }
    ret += "<" + Join(arg_names, ",") + ">";
  }
  if (IsArray()) {
    ret += "[]";
  }
  return ret;
}

string AidlTypeSpecifier::Signature() const {
  string ret = ToString();
  string annotations = AidlAnnotatable::ToString();
  if (annotations != "") {
    ret = annotations + " " + ret;
  }
  return ret;
}

bool AidlTypeSpecifier::Resolve(android::aidl::AidlTypenames& typenames) {
  assert(!IsResolved());
  pair<string, bool> result = typenames.ResolveTypename(unresolved_name_);
  if (result.second) {
    fully_qualified_name_ = result.first;
  }
  return result.second;
}

AidlVariableDeclaration::AidlVariableDeclaration(AidlTypeSpecifier* type, std::string name,
                                                 unsigned line)
    : AidlVariableDeclaration(type, name, line, nullptr /*default_value*/) {}

AidlVariableDeclaration::AidlVariableDeclaration(AidlTypeSpecifier* type, std::string name,
                                                 unsigned line, AidlConstantValue* default_value)
    : type_(type), name_(name), line_(line), default_value_(default_value) {}

bool AidlVariableDeclaration::CheckValid() const {
  if (default_value_ == nullptr) return true;

  const string given_type = type_->GetName();
  const string value_type = AidlConstantValue::ToString(default_value_->GetType());

  if (given_type != value_type) {
    cerr << "Declaration " << name_ << " is of type " << given_type << " but value is of type "
         << value_type << " on line " << line_ << endl;
    return false;
  }
  return true;
}

string AidlVariableDeclaration::ToString() const {
  string ret = type_->ToString() + " " + name_;
  if (default_value_ != nullptr) {
    ret += " = " + default_value_->ToString();
  }
  return ret;
}

string AidlVariableDeclaration::Signature() const {
  return type_->Signature() + " " + name_;
}

AidlArgument::AidlArgument(AidlArgument::Direction direction, AidlTypeSpecifier* type,
                           std::string name, unsigned line)
    : AidlVariableDeclaration(type, name, line),
      direction_(direction),
      direction_specified_(true) {}

AidlArgument::AidlArgument(AidlTypeSpecifier* type, std::string name, unsigned line)
    : AidlVariableDeclaration(type, name, line),
      direction_(AidlArgument::IN_DIR),
      direction_specified_(false) {}

string AidlArgument::GetDirectionSpecifier() const {
  string ret;
  if (direction_specified_) {
    switch(direction_) {
    case AidlArgument::IN_DIR:
      ret += "in ";
      break;
    case AidlArgument::OUT_DIR:
      ret += "out ";
      break;
    case AidlArgument::INOUT_DIR:
      ret += "inout ";
      break;
    }
  }
  return ret;
}

string AidlArgument::ToString() const {
  return GetDirectionSpecifier() + AidlVariableDeclaration::ToString();
}

std::string AidlArgument::Signature() const {
  return GetDirectionSpecifier() + AidlVariableDeclaration::Signature();
}

string AidlConstantValue::ToString(Type type) {
  switch (type) {
    case Type::INTEGER:
      return "int";
    case Type::STRING:
      return "String";
    case Type::ERROR:
      LOG(FATAL) << "aidl internal error: error type failed to halt program";
    default:
      LOG(FATAL) << "aidl internal error: unknown constant type: " << static_cast<int>(type);
      return "";  // not reached
  }
}

AidlConstantValue::AidlConstantValue(Type type, const std::string& checked_value)
    : type_(type), value_(checked_value) {}

AidlConstantValue* AidlConstantValue::LiteralInt(int32_t value) {
  return new AidlConstantValue(Type::INTEGER, std::to_string(value));
}

AidlConstantValue* AidlConstantValue::ParseHex(const std::string& value, unsigned line) {
  uint32_t unsigned_value;
  if (!android::base::ParseUint<uint32_t>(value.c_str(), &unsigned_value)) {
    cerr << "Found invalid int value '" << value << "' on line " << line << endl;
    return new AidlConstantValue(Type::ERROR, "");
  }

  return LiteralInt(unsigned_value);
}

AidlConstantValue* AidlConstantValue::ParseString(const std::string& value, unsigned line) {
  for (size_t i = 0; i < value.length(); ++i) {
    const char& c = value[i];
    if (c <= 0x1f || // control characters are < 0x20
        c >= 0x7f || // DEL is 0x7f
        c == '\\') { // Disallow backslashes for future proofing.
      cerr << "Found invalid character at index " << i << " in string constant '" << value
           << "' beginning on line " << line << endl;
      return new AidlConstantValue(Type::ERROR, "");
    }
  }

  return new AidlConstantValue(Type::STRING, value);
}

string AidlConstantValue::ToString() const {
  CHECK(type_ != Type::ERROR) << "aidl internal error: error should be checked " << value_;
  return value_;
}

AidlConstantDeclaration::AidlConstantDeclaration(AidlTypeSpecifier* type, std::string name,
                                                 AidlConstantValue* value, unsigned line)
    : type_(type), name_(name), value_(value), line_(line) {}

bool AidlConstantDeclaration::CheckValid() const {
  // Error message logged above
  if (value_->GetType() == AidlConstantValue::Type::ERROR) return false;

  if (type_->ToString() != AidlConstantValue::ToString(value_->GetType())) {
    cerr << "Constant " << name_ << " is of type " << type_->ToString() << " but value is of type "
         << AidlConstantValue::ToString(value_->GetType()) << " on line " << line_ << endl;
    return false;
  }

  return true;
}

AidlMethod::AidlMethod(bool oneway, AidlTypeSpecifier* type, std::string name,
                       std::vector<std::unique_ptr<AidlArgument>>* args, unsigned line,
                       const std::string& comments, int id)
    : oneway_(oneway),
      comments_(comments),
      type_(type),
      name_(name),
      line_(line),
      arguments_(std::move(*args)),
      id_(id) {
  has_id_ = true;
  delete args;
  for (const unique_ptr<AidlArgument>& a : arguments_) {
    if (a->IsIn()) { in_arguments_.push_back(a.get()); }
    if (a->IsOut()) { out_arguments_.push_back(a.get()); }
  }
}

AidlMethod::AidlMethod(bool oneway, AidlTypeSpecifier* type, std::string name,
                       std::vector<std::unique_ptr<AidlArgument>>* args, unsigned line,
                       const std::string& comments)
    : AidlMethod(oneway, type, name, args, line, comments, 0) {
  has_id_ = false;
}

string AidlMethod::Signature() const {
  vector<string> arg_signatures;
  for (const auto& arg : GetArguments()) {
    arg_signatures.emplace_back(arg->Signature());
  }
  return GetType().Signature() + " " + GetName() + "(" + Join(arg_signatures, ", ") + ")";
}

Parser::Parser(const IoDelegate& io_delegate, android::aidl::AidlTypenames* typenames)
    : io_delegate_(io_delegate), typenames_(typenames) {
  yylex_init(&scanner_);
}

AidlDefinedType::AidlDefinedType(std::string name, unsigned line, const std::string& comments,
                                 const std::vector<std::string>& package)
    : name_(name), line_(line), comments_(comments), package_(package) {}

std::string AidlDefinedType::GetPackage() const {
  return Join(package_, '.');
}

std::string AidlDefinedType::GetCanonicalName() const {
  if (package_.empty()) {
    return GetName();
  }
  return GetPackage() + "." + GetName();
}

AidlParcelable::AidlParcelable(AidlQualifiedName* name, unsigned line,
                               const std::vector<std::string>& package,
                               const std::string& cpp_header)
    : AidlDefinedType(name->GetDotName(), line, "" /*comments*/, package),
      name_(name),
      cpp_header_(cpp_header) {
  // Strip off quotation marks if we actually have a cpp header.
  if (cpp_header_.length() >= 2) {
    cpp_header_ = cpp_header_.substr(1, cpp_header_.length() - 2);
  }
}

void AidlParcelable::Write(CodeWriter* writer) const {
  writer->Write("parcelable %s ;\n", GetName().c_str());
}

AidlStructuredParcelable::AidlStructuredParcelable(
    AidlQualifiedName* name, unsigned line, const std::vector<std::string>& package,
    std::vector<std::unique_ptr<AidlVariableDeclaration>>* variables)
    : AidlParcelable(name, line, package, "" /*cpp_header*/), variables_(std::move(*variables)) {}

void AidlStructuredParcelable::Write(CodeWriter* writer) const {
  writer->Write("parcelable %s {\n", GetName().c_str());
  writer->Indent();
  for (const auto& field : GetFields()) {
    writer->Write("%s;\n", field->Signature().c_str());
  }
  writer->Dedent();
  writer->Write("}\n");
}

AidlInterface::AidlInterface(const std::string& name, unsigned line,
                             const std::string& comments, bool oneway,
                             std::vector<std::unique_ptr<AidlMember>>* members,
                             const std::vector<std::string>& package)
    : AidlDefinedType(name, line, comments, package),
      oneway_(oneway) {
  for (auto& member : *members) {
    AidlMember* local = member.release();
    AidlMethod* method = local->AsMethod();
    AidlConstantDeclaration* constant = local->AsConstantDeclaration();

    CHECK(method == nullptr || constant == nullptr);

    if (method) {
      methods_.emplace_back(method);
    } else if (constant) {
      constants_.emplace_back(constant);
    } else {
      LOG(FATAL) << "Member is neither method nor constant!";
    }
  }

  delete members;
}

void AidlInterface::Write(CodeWriter* writer) const {
  writer->Write("interface %s {\n", GetName().c_str());
  writer->Indent();
  for (const auto& method : GetMethods()) {
    writer->Write("%s;\n", method->Signature().c_str());
  }
  writer->Dedent();
  writer->Write("}\n");
}

AidlDefinedType* AidlDocument::ReleaseDefinedType() {
  if (defined_types_.size() == 0) {
    return nullptr;
  }

  if (defined_types_.size() > 1) {
    LOG(ERROR) << "AIDL only supports compiling one defined type per file.";
    return nullptr;
  }

  return defined_types_[0].release();
}

AidlQualifiedName::AidlQualifiedName(std::string term,
                                     std::string comments)
    : terms_({term}),
      comments_(comments) {
  if (term.find('.') != string::npos) {
    terms_ = Split(term, ".");
    for (const auto& term: terms_) {
      if (term.empty()) {
        LOG(FATAL) << "Malformed qualified identifier: '" << term << "'";
      }
    }
  }
}

void AidlQualifiedName::AddTerm(const std::string& term) {
  terms_.push_back(term);
}

AidlImport::AidlImport(const std::string& from,
                       const std::string& needed_class, unsigned line)
    : from_(from),
      needed_class_(needed_class),
      line_(line) {}

Parser::~Parser() {
  if (raw_buffer_) {
    yy_delete_buffer(buffer_, scanner_);
    raw_buffer_.reset();
  }
  yylex_destroy(scanner_);
}

bool Parser::ParseFile(const string& filename) {
  // Make sure we can read the file first, before trashing previous state.
  unique_ptr<string> new_buffer = io_delegate_.GetFileContents(filename);
  if (!new_buffer) {
    LOG(ERROR) << "Error while opening file for parsing: '" << filename << "'";
    return false;
  }

  // Throw away old parsing state if we have any.
  if (raw_buffer_) {
    yy_delete_buffer(buffer_, scanner_);
    raw_buffer_.reset();
  }

  raw_buffer_ = std::move(new_buffer);
  // We're going to scan this buffer in place, and yacc demands we put two
  // nulls at the end.
  raw_buffer_->append(2u, '\0');
  filename_ = filename;
  package_.reset();
  error_ = 0;
  document_.reset();

  buffer_ = yy_scan_buffer(&(*raw_buffer_)[0], raw_buffer_->length(), scanner_);

  if (yy::parser(this).parse() != 0 || error_ != 0)
    return false;

  if (document_.get() == nullptr) {
    LOG(ERROR) << "Parser succeeded but yielded no document!";
    return false;
  }
  return true;
}

std::vector<std::string> Parser::Package() const {
  if (!package_) {
    return {};
  }
  return package_->GetTerms();
}

void Parser::AddImport(AidlQualifiedName* name, unsigned line) {
  imports_.emplace_back(new AidlImport(this->FileName(),
                                       name->GetDotName(), line));
  delete name;
}

bool Parser::Resolve() {
  bool success = true;
  for (AidlTypeSpecifier* typespec : unresolved_typespecs_) {
    if (!typespec->Resolve(*typenames_)) {
      LOG(ERROR) << "Failed to resolve '" << typespec->GetUnresolvedName() << "' at "
                 << this->FileName() << ":" << typespec->GetLine();
      success = false;
      // don't stop to show more errors if any
    }
  }
  return success;
}

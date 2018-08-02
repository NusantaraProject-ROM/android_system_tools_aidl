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

AidlLocation::AidlLocation(const std::string& file, Point begin, Point end)
    : file_(file), begin_(begin), end_(end) {}

std::ostream& operator<<(std::ostream& os, const AidlLocation& l) {
  os << l.file_ << ":" << l.begin_.line << "." << l.begin_.column << "-";
  if (l.begin_.line != l.end_.line) {
    os << l.end_.line << ".";
  }
  os << l.end_.column;
  return os;
}

AidlNode::AidlNode(const AidlLocation& location) : location_(location) {}

AidlError::AidlError(bool fatal) : os_(std::cerr), fatal_(fatal) {
  os_ << "ERROR: ";
}

static const string kNullable("nullable");
static const string kUtf8("utf8");
static const string kUtf8InCpp("utf8InCpp");

static const set<string> kAnnotationNames{kNullable, kUtf8, kUtf8InCpp};

AidlAnnotation* AidlAnnotation::Parse(const AidlLocation& location, const string& name) {
  if (kAnnotationNames.find(name) == kAnnotationNames.end()) {
    std::ostringstream stream;
    stream << "'" << name << "' is not a recognized annotation. ";
    stream << "It must be one of:";
    for (const string& kv : kAnnotationNames) {
      stream << " " << kv;
    }
    stream << ".";
    AIDL_ERROR(location) << stream.str();
    return nullptr;
  }
  return new AidlAnnotation(location, name);
}

AidlAnnotation::AidlAnnotation(const AidlLocation& location, const string& name)
    : AidlNode(location), name_(name) {}

static bool HasAnnotation(const set<unique_ptr<AidlAnnotation>>& annotations, const string& name) {
  for (const auto& a : annotations) {
    if (a->GetName() == name) {
      return true;
    }
  }
  return false;
}

AidlAnnotatable::AidlAnnotatable(const AidlLocation& location) : AidlNode(location) {}

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

AidlTypeSpecifier::AidlTypeSpecifier(const AidlLocation& location, const string& unresolved_name,
                                     bool is_array,
                                     vector<unique_ptr<AidlTypeSpecifier>>* type_params,
                                     const string& comments)
    : AidlAnnotatable(location),
      unresolved_name_(unresolved_name),
      is_array_(is_array),
      type_params_(type_params),
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

bool AidlTypeSpecifier::CheckValid() const {
  if (IsGeneric()) {
    const string& type_name = GetName();
    const int num = GetTypeParameters().size();
    if (type_name == "List") {
      if (num > 1) {
        cerr << " List cannot have type parameters more than one, but got "
             << "'" << ToString() << "'" << endl;
        return false;
      }
    } else if (type_name == "Map") {
      if (num != 0 && num != 2) {
        cerr << "Map must have 0 or 2 type parameters, but got "
             << "'" << ToString() << "'" << endl;
        return false;
      }
    }
  }
  return true;
}

AidlVariableDeclaration::AidlVariableDeclaration(const AidlLocation& location,
                                                 AidlTypeSpecifier* type, const std::string& name)
    : AidlVariableDeclaration(location, type, name, nullptr /*default_value*/) {}

AidlVariableDeclaration::AidlVariableDeclaration(const AidlLocation& location,
                                                 AidlTypeSpecifier* type, const std::string& name,
                                                 AidlConstantValue* default_value)
    : AidlNode(location), type_(type), name_(name), default_value_(default_value) {}

bool AidlVariableDeclaration::CheckValid() const {
  if (!type_->CheckValid()) {
    return false;
  }

  if (default_value_ == nullptr) return true;

  const string given_type = type_->GetName();
  const string value_type = AidlConstantValue::ToString(default_value_->GetType());

  if (given_type != value_type) {
    AIDL_ERROR(*this) << "Declaration " << name_ << " is of type " << given_type
                      << " but value is of type " << value_type << endl;
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

AidlArgument::AidlArgument(const AidlLocation& location, AidlArgument::Direction direction,
                           AidlTypeSpecifier* type, const std::string& name)
    : AidlVariableDeclaration(location, type, name),
      direction_(direction),
      direction_specified_(true) {}

AidlArgument::AidlArgument(const AidlLocation& location, AidlTypeSpecifier* type,
                           const std::string& name)
    : AidlVariableDeclaration(location, type, name),
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
  class AidlInterface;
  class AidlInterface;
  class AidlParcelable;
  class AidlStructuredParcelable;
  class AidlParcelable;
  class AidlStructuredParcelable;
  return GetDirectionSpecifier() + AidlVariableDeclaration::Signature();
}

AidlMember::AidlMember(const AidlLocation& location) : AidlNode(location) {}

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

AidlConstantValue::AidlConstantValue(const AidlLocation& location, Type type,
                                     const std::string& checked_value)
    : AidlNode(location), type_(type), value_(checked_value) {}

AidlConstantValue* AidlConstantValue::LiteralInt(const AidlLocation& location, int32_t value) {
  return new AidlConstantValue(location, Type::INTEGER, std::to_string(value));
}

AidlConstantValue* AidlConstantValue::ParseHex(const AidlLocation& location,
                                               const std::string& value) {
  uint32_t unsigned_value;
  if (!android::base::ParseUint<uint32_t>(value.c_str(), &unsigned_value)) {
    AIDL_ERROR(location) << "Found invalid int value '" << value << "'";
    return new AidlConstantValue(location, Type::ERROR, "");
  }

  return LiteralInt(location, unsigned_value);
}

AidlConstantValue* AidlConstantValue::ParseString(const AidlLocation& location,
                                                  const std::string& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    const char& c = value[i];
    if (c <= 0x1f || // control characters are < 0x20
        c >= 0x7f || // DEL is 0x7f
        c == '\\') { // Disallow backslashes for future proofing.
      AIDL_ERROR(location) << "Found invalid character at index " << i << " in string constant '"
                           << value << "'";
      return new AidlConstantValue(location, Type::ERROR, "");
    }
  }

  return new AidlConstantValue(location, Type::STRING, value);
}

string AidlConstantValue::ToString() const {
  CHECK(type_ != Type::ERROR) << "aidl internal error: error should be checked " << value_;
  return value_;
}

AidlConstantDeclaration::AidlConstantDeclaration(const AidlLocation& location,
                                                 AidlTypeSpecifier* type, const std::string& name,
                                                 AidlConstantValue* value)
    : AidlMember(location), type_(type), name_(name), value_(value) {}

bool AidlConstantDeclaration::CheckValid() const {
  // Error message logged above
  if (value_->GetType() == AidlConstantValue::Type::ERROR) return false;

  if (type_->ToString() != AidlConstantValue::ToString(value_->GetType())) {
    AIDL_ERROR(this) << "Constant " << name_ << " is of type " << type_->ToString()
                     << " but value is of type " << AidlConstantValue::ToString(value_->GetType());
    return false;
  }

  return true;
}

AidlMethod::AidlMethod(const AidlLocation& location, bool oneway, AidlTypeSpecifier* type,
                       const std::string& name, std::vector<std::unique_ptr<AidlArgument>>* args,
                       const std::string& comments)
    : AidlMethod(location, oneway, type, name, args, comments, 0, true) {
  has_id_ = false;
}

AidlMethod::AidlMethod(const AidlLocation& location, bool oneway, AidlTypeSpecifier* type,
                       const std::string& name, std::vector<std::unique_ptr<AidlArgument>>* args,
                       const std::string& comments, int id)
    : AidlMethod(location, oneway, type, name, args, comments, id, true) {}

AidlMethod::AidlMethod(const AidlLocation& location, bool oneway, AidlTypeSpecifier* type,
                       const std::string& name, std::vector<std::unique_ptr<AidlArgument>>* args,
                       const std::string& comments, int id, bool is_user_defined)
    : AidlMember(location),
      oneway_(oneway),
      comments_(comments),
      type_(type),
      name_(name),
      arguments_(std::move(*args)),
      id_(id),
      is_user_defined_(is_user_defined) {
  has_id_ = true;
  delete args;
  for (const unique_ptr<AidlArgument>& a : arguments_) {
    if (a->IsIn()) { in_arguments_.push_back(a.get()); }
    if (a->IsOut()) { out_arguments_.push_back(a.get()); }
  }
}


string AidlMethod::Signature() const {
  vector<string> arg_signatures;
  for (const auto& arg : GetArguments()) {
    arg_signatures.emplace_back(arg->GetType().ToString());
  }
  return GetName() + "(" + Join(arg_signatures, ", ") + ")";
}

string AidlMethod::ToString() const {
  vector<string> arg_strings;
  for (const auto& arg : GetArguments()) {
    arg_strings.emplace_back(arg->Signature());
  }
  return GetType().Signature() + " " + GetName() + "(" + Join(arg_strings, ", ") + ")";
}

AidlDefinedType::AidlDefinedType(const AidlLocation& location, const std::string& name,
                                 const std::string& comments,
                                 const std::vector<std::string>& package)
    : AidlAnnotatable(location), name_(name), comments_(comments), package_(package) {}

std::string AidlDefinedType::GetPackage() const {
  return Join(package_, '.');
}

std::string AidlDefinedType::GetCanonicalName() const {
  if (package_.empty()) {
    return GetName();
  }
  return GetPackage() + "." + GetName();
}

AidlParcelable::AidlParcelable(const AidlLocation& location, AidlQualifiedName* name,
                               const std::vector<std::string>& package,
                               const std::string& cpp_header)
    : AidlDefinedType(location, name->GetDotName(), "" /*comments*/, package),
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
    const AidlLocation& location, AidlQualifiedName* name, const std::vector<std::string>& package,
    std::vector<std::unique_ptr<AidlVariableDeclaration>>* variables)
    : AidlParcelable(location, name, package, "" /*cpp_header*/),
      variables_(std::move(*variables)) {}

void AidlStructuredParcelable::Write(CodeWriter* writer) const {
  writer->Write("parcelable %s {\n", GetName().c_str());
  writer->Indent();
  for (const auto& field : GetFields()) {
    writer->Write("%s;\n", field->Signature().c_str());
  }
  writer->Dedent();
  writer->Write("}\n");
}

AidlInterface::AidlInterface(const AidlLocation& location, const std::string& name,
                             const std::string& comments, bool oneway,
                             std::vector<std::unique_ptr<AidlMember>>* members,
                             const std::vector<std::string>& package)
    : AidlDefinedType(location, name, comments, package), oneway_(oneway) {
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
      AIDL_FATAL(this) << "Member is neither method nor constant!";
    }
  }

  delete members;
}

void AidlInterface::Write(CodeWriter* writer) const {
  writer->Write("interface %s {\n", GetName().c_str());
  writer->Indent();
  for (const auto& method : GetMethods()) {
    writer->Write("%s;\n", method->ToString().c_str());
  }
  writer->Dedent();
  writer->Write("}\n");
}

AidlQualifiedName::AidlQualifiedName(const AidlLocation& location, const std::string& term,
                                     const std::string& comments)
    : AidlNode(location), terms_({term}), comments_(comments) {
  if (term.find('.') != string::npos) {
    terms_ = Split(term, ".");
    for (const auto& subterm : terms_) {
      if (subterm.empty()) {
        AIDL_FATAL(this) << "Malformed qualified identifier: '" << term << "'";
      }
    }
  }
}

void AidlQualifiedName::AddTerm(const std::string& term) {
  terms_.push_back(term);
}

AidlImport::AidlImport(const AidlLocation& location, const std::string& needed_class)
    : AidlNode(location), needed_class_(needed_class) {}

Parser::Parser(const IoDelegate& io_delegate, android::aidl::AidlTypenames& typenames)
    : io_delegate_(io_delegate), typenames_(typenames) {
  yylex_init(&scanner_);
}

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
    AIDL_ERROR(filename) << "Error while opening file for parsing";
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

  buffer_ = yy_scan_buffer(&(*raw_buffer_)[0], raw_buffer_->length(), scanner_);

  if (yy::parser(this).parse() != 0 || error_ != 0)
    return false;

  return true;
}

std::vector<std::string> Parser::Package() const {
  if (!package_) {
    return {};
  }
  return package_->GetTerms();
}

void Parser::AddImport(AidlImport* import) {
  imports_.emplace_back(import);
}

bool Parser::Resolve() {
  bool success = true;
  for (AidlTypeSpecifier* typespec : unresolved_typespecs_) {
    if (!typespec->Resolve(typenames_)) {
      AIDL_ERROR(typespec) << "Failed to resolve '" << typespec->GetUnresolvedName() << "'";
      success = false;
      // don't stop to show more errors if any
    }
  }
  return success;
}

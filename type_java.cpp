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

#include "type_java.h"

#include <sys/types.h>

#include <android-base/strings.h>

#include "aidl_language.h"
#include "logging.h"

using std::string;
using android::base::Split;
using android::base::Join;
using android::base::Trim;

namespace android {
namespace aidl {
namespace java {

Expression* NULL_VALUE;
Expression* THIS_VALUE;
Expression* SUPER_VALUE;
Expression* TRUE_VALUE;
Expression* FALSE_VALUE;

// ================================================================

Type::Type(const JavaTypeNamespace* types, const string& name, int kind, bool canWriteToParcel)
    : Type(types, "", name, kind, canWriteToParcel, "", -1) {}

Type::Type(const JavaTypeNamespace* types, const string& package, const string& name, int kind,
           bool canWriteToParcel, const string& declFile, int declLine)
    : ValidatableType(kind, package, name, declFile, declLine),
      m_types(types),
      m_javaType((package.empty()) ? name : package + "." + name),
      m_canWriteToParcel(canWriteToParcel) {}

string Type::InstantiableName() const { return JavaType(); }

Expression* Type::BuildWriteToParcelFlags(int flags) const {
  if (flags == 0) {
    return new LiteralExpression("0");
  }
  if ((flags & PARCELABLE_WRITE_RETURN_VALUE) != 0) {
    return new FieldVariable(m_types->ParcelableInterfaceType(),
                             "PARCELABLE_WRITE_RETURN_VALUE");
  }
  return new LiteralExpression("0");
}

// ================================================================

BasicType::BasicType(const JavaTypeNamespace* types, const string& name,
                     const string& marshallParcel, const string& unmarshallParcel,
                     const string& writeArrayParcel, const string& createArrayParcel,
                     const string& readArrayParcel)
    : Type(types, name, ValidatableType::KIND_BUILT_IN, true),
      m_marshallParcel(marshallParcel),
      m_unmarshallParcel(unmarshallParcel) {
  m_array_type.reset(new BasicArrayType(types, name, writeArrayParcel,
                                        createArrayParcel, readArrayParcel));
}

BasicArrayType::BasicArrayType(const JavaTypeNamespace* types, const string& name,
                               const string& writeArrayParcel, const string& createArrayParcel,
                               const string& readArrayParcel)
    : Type(types, name, ValidatableType::KIND_BUILT_IN, true),
      m_writeArrayParcel(writeArrayParcel),
      m_createArrayParcel(createArrayParcel),
      m_readArrayParcel(readArrayParcel) {}

// ================================================================

FileDescriptorType::FileDescriptorType(const JavaTypeNamespace* types)
    : Type(types, "java.io", "FileDescriptor", ValidatableType::KIND_BUILT_IN, true) {
  m_array_type.reset(new FileDescriptorArrayType(types));
}

FileDescriptorArrayType::FileDescriptorArrayType(const JavaTypeNamespace* types)
    : Type(types, "java.io", "FileDescriptor", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

ParcelFileDescriptorType::ParcelFileDescriptorType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "ParcelFileDescriptor", ValidatableType::KIND_BUILT_IN, true) {
  m_array_type.reset(new ParcelFileDescriptorArrayType(types));
}

ParcelFileDescriptorArrayType::ParcelFileDescriptorArrayType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "ParcelFileDescriptor", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

BooleanType::BooleanType(const JavaTypeNamespace* types)
    : Type(types, "boolean", ValidatableType::KIND_BUILT_IN, true) {
  m_array_type.reset(new BooleanArrayType(types));
}

BooleanArrayType::BooleanArrayType(const JavaTypeNamespace* types)
    : Type(types, "boolean", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

CharType::CharType(const JavaTypeNamespace* types)
    : Type(types, "char", ValidatableType::KIND_BUILT_IN, true) {
  m_array_type.reset(new CharArrayType(types));
}

CharArrayType::CharArrayType(const JavaTypeNamespace* types)
    : Type(types, "char", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

StringType::StringType(const JavaTypeNamespace* types, const std::string& package,
                       const std::string& class_name)
    : Type(types, package, class_name, ValidatableType::KIND_BUILT_IN, true) {
  m_array_type.reset(new StringArrayType(types));
}

StringArrayType::StringArrayType(const JavaTypeNamespace* types)
    : Type(types, "java.lang", "String", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

CharSequenceType::CharSequenceType(const JavaTypeNamespace* types)
    : Type(types, "java.lang", "CharSequence", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

RemoteExceptionType::RemoteExceptionType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "RemoteException", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

RuntimeExceptionType::RuntimeExceptionType(const JavaTypeNamespace* types)
    : Type(types, "java.lang", "RuntimeException", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

IBinderType::IBinderType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "IBinder", ValidatableType::KIND_BUILT_IN, true) {
  m_array_type.reset(new IBinderArrayType(types));
}

IBinderArrayType::IBinderArrayType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "IBinder", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

IInterfaceType::IInterfaceType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "IInterface", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

BinderType::BinderType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "Binder", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

BinderProxyType::BinderProxyType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "BinderProxy", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

ParcelType::ParcelType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "Parcel", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

ParcelableInterfaceType::ParcelableInterfaceType(const JavaTypeNamespace* types)
    : Type(types, "android.os", "Parcelable", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

MapType::MapType(const JavaTypeNamespace* types)
    : Type(types, "java.util", "Map", ValidatableType::KIND_BUILT_IN, true) {}

// ================================================================

ListType::ListType(const JavaTypeNamespace* types)
    : Type(types, "java.util", "List", ValidatableType::KIND_BUILT_IN, true) {}

string ListType::InstantiableName() const { return "java.util.ArrayList"; }

// ================================================================

UserDataType::UserDataType(const JavaTypeNamespace* types, const string& package,
                           const string& name, bool builtIn, bool canWriteToParcel,
                           const string& declFile, int declLine)
    : Type(types, package, name,
           builtIn ? ValidatableType::KIND_BUILT_IN : ValidatableType::KIND_PARCELABLE,
           canWriteToParcel, declFile, declLine) {
  m_array_type.reset(new UserDataArrayType(types, package, name, builtIn,
                                           canWriteToParcel, declFile,
                                           declLine));
}

UserDataArrayType::UserDataArrayType(const JavaTypeNamespace* types, const string& package,
                                     const string& name, bool builtIn, bool canWriteToParcel,
                                     const string& declFile, int declLine)
    : Type(types, package, name,
           builtIn ? ValidatableType::KIND_BUILT_IN : ValidatableType::KIND_PARCELABLE,
           canWriteToParcel, declFile, declLine) {}

// ================================================================

InterfaceType::InterfaceType(const JavaTypeNamespace* types, const string& package,
                             const string& name, bool builtIn, const string& declFile, int declLine,
                             const Type* stub, const Type* proxy, const Type* defaultImpl)
    : Type(types, package, name,
           builtIn ? ValidatableType::KIND_BUILT_IN : ValidatableType::KIND_INTERFACE, true,
           declFile, declLine),
      stub_(stub),
      proxy_(proxy),
      defaultImpl_(defaultImpl) {}

// ================================================================

GenericListType::GenericListType(const JavaTypeNamespace* types, const Type* contained_type)
    : Type(types, "java.util", "List<" + contained_type->CanonicalName() + ">",
           ValidatableType::KIND_BUILT_IN, true),
      m_contained_type(contained_type) {}

string GenericListType::InstantiableName() const {
  return "java.util.ArrayList<" + m_contained_type->JavaType() + ">";
}

// ================================================================

ClassLoaderType::ClassLoaderType(const JavaTypeNamespace* types)
    : Type(types, "java.lang", "ClassLoader", ValidatableType::KIND_BUILT_IN, false) {}

// ================================================================

void JavaTypeNamespace::Init() {
  Add(new BasicType(this, "void", "XXX", "XXX", "XXX", "XXX", "XXX"));

  m_bool_type = new BooleanType(this);
  Add(m_bool_type);

  Add(new BasicType(this, "byte", "writeByte", "readByte", "writeByteArray", "createByteArray",
                    "readByteArray"));

  Add(new CharType(this));

  m_int_type = new BasicType(this, "int", "writeInt", "readInt", "writeIntArray", "createIntArray",
                             "readIntArray");
  Add(m_int_type);

  Add(new BasicType(this, "long", "writeLong", "readLong", "writeLongArray", "createLongArray",
                    "readLongArray"));

  Add(new BasicType(this, "float", "writeFloat", "readFloat", "writeFloatArray", "createFloatArray",
                    "readFloatArray"));

  Add(new BasicType(this, "double", "writeDouble", "readDouble", "writeDoubleArray",
                    "createDoubleArray", "readDoubleArray"));

  m_string_type = new class StringType(this, "java.lang", "String");
  Add(m_string_type);
  Add(new class StringType(this, ::android::aidl::kAidlReservedTypePackage,
                           ::android::aidl::kUtf8InCppStringClass));

  Add(new Type(this, "java.lang", "Object", ValidatableType::KIND_BUILT_IN, false));

  Add(new FileDescriptorType(this));

  Add(new ParcelFileDescriptorType(this));

  Add(new CharSequenceType(this));

  Add(new MapType(this));

  Add(new ListType(this));

  m_text_utils_type =
      new Type(this, "android.text", "TextUtils", ValidatableType::KIND_BUILT_IN, false);
  Add(m_text_utils_type);

  m_remote_exception_type = new class RemoteExceptionType(this);
  Add(m_remote_exception_type);

  m_runtime_exception_type = new class RuntimeExceptionType(this);
  Add(m_runtime_exception_type);

  m_ibinder_type = new class IBinderType(this);
  Add(m_ibinder_type);

  m_iinterface_type = new class IInterfaceType(this);
  Add(m_iinterface_type);

  m_binder_native_type = new class BinderType(this);
  Add(m_binder_native_type);

  m_binder_proxy_type = new class BinderProxyType(this);
  Add(m_binder_proxy_type);

  m_parcel_type = new class ParcelType(this);
  Add(m_parcel_type);

  m_parcelable_interface_type = new class ParcelableInterfaceType(this);
  Add(m_parcelable_interface_type);

  m_context_type =
      new class Type(this, "android.content", "Context", ValidatableType::KIND_BUILT_IN, false);
  Add(m_context_type);

  m_classloader_type = new class ClassLoaderType(this);
  Add(m_classloader_type);

  NULL_VALUE = new LiteralExpression("null");
  THIS_VALUE = new LiteralExpression("this");
  SUPER_VALUE = new LiteralExpression("super");
  TRUE_VALUE = new LiteralExpression("true");
  FALSE_VALUE = new LiteralExpression("false");
}

bool JavaTypeNamespace::AddParcelableType(const AidlParcelable& p,
                                          const std::string& filename) {
  Type* type = new UserDataType(this, p.GetPackage(), p.GetName(), false, true, filename);
  return Add(type);
}

bool JavaTypeNamespace::AddBinderType(const AidlInterface& b,
                                      const std::string& filename) {
  // for interfaces, add the stub, proxy, and interface types.
  Type* stub = new Type(this, b.GetPackage(), b.GetName() + ".Stub",
                        ValidatableType::KIND_GENERATED, false, filename);
  Type* proxy = new Type(this, b.GetPackage(), b.GetName() + ".Stub.Proxy",
                         ValidatableType::KIND_GENERATED, false, filename);
  Type* defaultImpl = new Type(this, b.GetPackage(), b.GetName() + ".Default",
                               ValidatableType::KIND_GENERATED, false, filename);
  Type* type = new InterfaceType(this, b.GetPackage(), b.GetName(), false, filename, -1, stub,
                                 proxy, defaultImpl);

  bool success = true;
  success &= Add(type);
  success &= Add(stub);
  success &= Add(proxy);
  success &= Add(defaultImpl);
  return success;
}

bool JavaTypeNamespace::AddListType(const std::string& contained_type_name) {
  const Type* contained_type = FindTypeByCanonicalName(contained_type_name);
  if (!contained_type) {
    return false;
  }
  Add(new GenericListType(this, contained_type));
  return true;
}

bool JavaTypeNamespace::AddMapType(const string& /*key_type_name*/,
                                   const string& /*value_type_name*/) {
  LOG(ERROR) << "Don't know how to create a Map<K,V> container.";
  return false;
}

}  // namespace java
}  // namespace aidl
}  // namespace android

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
#pragma once

#include "aidl_language.h"

namespace android {
namespace aidl {
namespace cpp {

// This header provides functions that translate AIDL things to cpp things.

std::string ConstantValueDecorator(const AidlTypeSpecifier& type, const std::string& raw_value);

}  // namespace cpp
}  // namespace aidl
}  // namespace android

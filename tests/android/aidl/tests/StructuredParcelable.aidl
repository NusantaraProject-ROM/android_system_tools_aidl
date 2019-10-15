/*
 * Copyright (C) 2018 The Android Open Source Project
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

package android.aidl.tests;

import android.aidl.tests.ByteEnum;
import android.aidl.tests.IntEnum;
import android.aidl.tests.LongEnum;

parcelable StructuredParcelable {
    int[] shouldContainThreeFs;
    int f;
    @utf8InCpp String shouldBeJerry;
    ByteEnum shouldBeByteBar;
    IntEnum shouldBeIntBar;
    LongEnum shouldBeLongBar;
    ByteEnum[] shouldContainTwoByteFoos;
    IntEnum[] shouldContainTwoIntFoos;
    LongEnum[] shouldContainTwoLongFoos;

    String stringDefaultsToFoo = "foo";
    byte byteDefaultsToFour = 4;
    int intDefaultsToFive = 5;
    long longDefaultsToNegativeSeven = -7;
    boolean booleanDefaultsToTrue = true;
    char charDefaultsToC = 'C';
    float floatDefaultsToPi = 3.14f;
    double doubleWithDefault = -3.14e17;
    int[] arrayDefaultsTo123 = { 1, 2, 3 };
    int[] arrayDefaultsToEmpty = { };

    // parse checks only
    double checkDoubleFromFloat = 3.14f;
    String[] checkStringArray1 = { "a", "b" };
    @utf8InCpp String[] checkStringArray2 = { "a", "b" };

    // Add test to verify corner cases
    int int32_min = -2147483648;
    int int32_max =  2147483647;
    long int64_max =  9223372036854775807;
    int hexInt32_neg_1 = 0xffffffff;

    @nullable IBinder ibinder;
}

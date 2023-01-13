// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.util.json;

import com.google.gson.JsonElement;

public interface Checker {
  boolean check(JsonElement element, ConflictCollector collector);

  static boolean checkNotNull(JsonElement element, ConflictCollector collector) {
    if (element == null) {
      collector.addConflict("doesn't exist");
      return false;
    }
    return true;
  }

  static void unexpectedTypeConflict(
      JsonElement element, Class<?> clazz, ConflictCollector collector) {
    collector.addConflict(String.format(
        "expecting '%s' type but '%s' found %s",
        clazz.getSimpleName(), element.getClass().getSimpleName(), element.toString()));
  }

  static <T> T castToExpectedType(
      JsonElement element, Class<T> clazz, ConflictCollector collector) {
    if (!checkNotNull(element, collector)) {
      return null;
    }
    if (!clazz.isInstance(element)) {
      unexpectedTypeConflict(element, clazz, collector);
      return null;
    }

    return clazz.cast(element);
  }
}

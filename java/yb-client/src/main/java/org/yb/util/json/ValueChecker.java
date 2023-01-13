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
import com.google.gson.JsonPrimitive;

public abstract class ValueChecker<T extends Comparable<?>> implements Checker {
  private final Class<T> clazz;

  protected ValueChecker(Class<T> clazz) {
    this.clazz = clazz;
  }

  public final boolean check(JsonElement element, ConflictCollector collector) {
    if (!Checker.checkNotNull(element, collector)) {
      return false;
    }
    if (element.isJsonPrimitive()) {
      JsonPrimitive primitive = element.getAsJsonPrimitive();
      Object value = null;
      if (primitive.isString()) {
        value = primitive.getAsString();
      } else if (primitive.isBoolean()) {
        value = primitive.getAsBoolean();
      } else if (primitive.isNumber()) {
        value = clazz.equals(Double.class)
            ? (Object)primitive.getAsDouble() : (Object)primitive.getAsLong();
      }
      if (clazz.isInstance(value)) {
        return checkValue(clazz.cast(value), collector);
      }
    }
    Checker.unexpectedTypeConflict(element, clazz, collector);
    return false;
  }

  abstract protected boolean checkValue(T value, ConflictCollector collector);
}

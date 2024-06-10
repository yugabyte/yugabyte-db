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

import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonElement;

public final class JsonUtil {
  private static final Checker ABSENCE_CHECKER = new AbsenceChecker();

  public static String asPrettyString(JsonElement element) {
    return new GsonBuilder().setPrettyPrinting().create().toJson(element);
  }

  public static List<String> findConflicts(
      JsonElement element, Checker checker, char propertyNameQuote) {
    ArrayList<String> conflicts = new ArrayList<>();
    checker.check(element, new ConflictCollector(conflicts, propertyNameQuote));
    return conflicts;
  }

  public static List<String> findConflicts(JsonElement element, Checker checker) {
    return findConflicts(element, checker, '"');
  }

  public static <T extends ObjectCheckerBuilder> T makeCheckerBuilder(
      Class<T> clazz, boolean nullify) {
    return clazz.cast(Proxy.newProxyInstance(
        clazz.getClassLoader(),
        new Class[] { clazz },
        new DynamicObjectCheckerBuilder<>(clazz, nullify)));
  }

  public static <T extends ObjectCheckerBuilder> T makeCheckerBuilder(Class<T> clazz) {
    return makeCheckerBuilder(clazz, true);
  }

  public static Checker absenceCheckerOnNull(Checker checker) {
    return checker == null ? ABSENCE_CHECKER : checker;
  }
}

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

import java.util.Map;

import com.google.gson.JsonObject;

final class BasicObjectChecker extends ObjectChecker {
  private final Map<String, Checker> checkers;

  BasicObjectChecker(Map<String, Checker> checkers) {
    this.checkers = checkers;
  }

  @Override
  protected boolean checkObject(JsonObject jsonObject, ConflictCollector collector) {
    boolean result = true;
    for (Map.Entry<String, Checker> entry : checkers.entrySet()) {
      try {
        String property = entry.getKey();
        collector.pushIdentity(property);
        result = JsonUtil.absenceCheckerOnNull(entry.getValue())
            .check(jsonObject.get(property), collector) && result;
      } finally {
        collector.popIdentity();
      }
    }
    return result;
  }
}

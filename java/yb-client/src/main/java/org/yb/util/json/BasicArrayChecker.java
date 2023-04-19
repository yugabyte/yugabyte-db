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

import java.util.List;

import com.google.gson.JsonArray;

final class BasicArrayChecker extends ArrayChecker {
  private final List<Checker> checkers;

  BasicArrayChecker(List<Checker> checkers) {
    this.checkers = checkers;
  }

  @Override
  protected boolean checkArray(JsonArray jsonArray, ConflictCollector collector) {
    if (jsonArray.size() != checkers.size()) {
      collector.addConflict(String.format(
          "%d items expected but %d found", checkers.size(), jsonArray.size()));
      return false;
    }
    boolean result = true;
    int index = 0;
    for (Checker checker : checkers) {
      try {
        collector.pushIdentity(index);
        result = checker.check(jsonArray.get(index), collector) && result;
      } finally {
        collector.popIdentity();
      }
      ++index;
    }
    return result;
  }
}

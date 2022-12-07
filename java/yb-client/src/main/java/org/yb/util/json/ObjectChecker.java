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
import com.google.gson.JsonObject;

public abstract class ObjectChecker implements Checker {
  @Override
  public boolean check(JsonElement element, ConflictCollector collector) {
    JsonObject jsonObject = Checker.castToExpectedType(element, JsonObject.class, collector);
    return jsonObject != null && checkObject(jsonObject, collector);
  }

  protected abstract boolean checkObject(JsonObject jsonObject, ConflictCollector collector);
}

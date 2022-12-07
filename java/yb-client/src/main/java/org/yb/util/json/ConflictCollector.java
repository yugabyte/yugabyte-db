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

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.List;

public final class ConflictCollector {
  private final List<String> conflicts;
  private final char propertyNameQuote;
  private final Deque<Object> identities = new ArrayDeque<>();

  public ConflictCollector(List<String> conflicts, char propertyNameQuote) {
    this.conflicts = conflicts;
    this.propertyNameQuote = propertyNameQuote;
  }

  public void pushIdentity(String name) {
    identities.addLast(name);
  }

  public void pushIdentity(Integer idx) {
    identities.addLast(idx);
  }

  public void popIdentity() {
    identities.removeLast();
  }

  public void addConflict(String description) {
    StringBuilder builder = new StringBuilder();
    for (Object identity : identities) {
      if (builder.length() != 0) {
        builder.append(".");
      }
      if (identity instanceof Integer) {
        builder.append('[').append(identity).append(']');
      } else {
        builder.append(propertyNameQuote).append(identity).append(propertyNameQuote);
      }
    }
    conflicts.add(builder.append(": ").append(description).toString());
  }
}

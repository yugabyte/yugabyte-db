// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import java.util.List;
import java.util.Map;

import org.yb.util.Pair;

public class PipelineResult {
  private final Map<String, List<List<String>>> rowsByLabel;
  private final List<Pair<String, String>> parameterStatuses;

  PipelineResult(Map<String, List<List<String>>> rowsByLabel,
                 List<Pair<String, String>> parameterStatuses) {
    this.rowsByLabel = rowsByLabel;
    this.parameterStatuses = parameterStatuses;
  }

  public String value(String label) {
    List<List<String>> rows = rowsByLabel.get(label);
    if (rows == null) {
      throw new IllegalArgumentException("No step labelled \"" + label + "\" produced rows");
    }
    if (rows.isEmpty()) {
      throw new IllegalArgumentException("Step \"" + label + "\" returned no rows");
    }
    return rows.get(0).get(0);
  }

  public int intValue(String label) {
    return Integer.parseInt(value(label));
  }

  // (name, value) of every ParameterStatus this pipeline received, in arrival order.
  public List<Pair<String, String>> parameterStatuses() {
    return parameterStatuses;
  }
}

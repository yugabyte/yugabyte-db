/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */
package org.yb.minicluster;

public enum MiniYBDaemonType {
  MASTER {
    @Override
    public String shortStr() { return "m"; }

    @Override
    public String humanReadableName() { return "master"; }
  },
  TSERVER {
    @Override
    public String shortStr() { return "ts"; }

    @Override
    public String humanReadableName() { return "tablet server"; }
  },
  YBCONTROLLER {
    @Override
    public String shortStr() {
      return "yb-controller";
    }

    @Override
    public String humanReadableName() {
      return "yb controller server";
    }
  },
  YUGABYTED {

    @Override
    public String shortStr() {
        return "ybd";
    }

    @Override
    public String humanReadableName() {
        return "yugabyted";
    }
  };

  abstract String shortStr();
  abstract String humanReadableName();
}

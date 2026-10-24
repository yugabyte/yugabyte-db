// Copyright (c) YugabyteDB, Inc.
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
package org.yb.cql;

import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestAuthenticationWithCachePasswords extends BaseAuthenticationCQLTest {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("password_hash_cache_size", "4");
    return flags;
  }

  @Test
  public void testMultipleLoginWithinCache() throws Exception {
    int roleCount = 4;
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      for (int i = 0; i < roleCount; i++) {
        String role = String.format("cache_role%d", i);
        createRole(cs.getSession(), role, password, true, false, true);
      }
    }

    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < roleCount; i++) {
        String role = String.format("cache_role%d", i);
        try (ClusterAndSession cs1 = connectWithCredentials(role, password)) {
          // Nothing to do here.
        }
      }
    }
  }

  @Test(timeout=500000)
  public void testLoginExhaustCache() throws Exception {
    int roleCount = 10;
    String password = "abc";

    try (ClusterAndSession cs = connectWithTestDefaults()) {
      for (int i = 0; i < roleCount; i++) {
        String role = String.format("exhaust_role%d", i);
        createRole(cs.getSession(), role, password, true, false, true);
      }
    }

    for (int i = 0; i < roleCount; i++) {
      String role = String.format("exhaust_role%d", i);
      try (ClusterAndSession cs1 = connectWithCredentials(role, password)) {
        // Nothing to do here.
      }
    }
  }
}

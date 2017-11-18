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
package org.yb.cql;

import com.datastax.driver.core.*;
import com.datastax.driver.core.ProtocolOptions.Compression;

import java.util.Arrays;

import org.junit.BeforeClass;
import org.junit.Test;

import static org.junit.Assert.*;

public class TestAuthentication extends BaseCQLTest {
  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    // Setting verbose level for debugging
    BaseCQLTest.tserverArgs = Arrays.asList("--use_cassandra_authentication=true");
    BaseCQLTest.setUpBeforeClass();
  }

  public Cluster.Builder getDefaultClusterBuilder() {
    // Default return cassandra/cassandra auth.
    return super.getDefaultClusterBuilder().withCredentials("cassandra", "cassandra");
  }

  @Test(timeout = 100000)
  public void testConnectWithDefaultUserPass() throws Exception {
    checkConnectivity(true, "cassandra", "cassandra", false);
  }

  @Test(timeout = 100000)
  public void testConnectWithFakeUserPass() throws Exception {
    checkConnectivity(true, "fakeUser", "fakePass", true);
  }

  @Test(timeout = 100000)
  public void testConnectNoUserPass() throws Exception {
    checkConnectivity(false, null, null, true);
  }

  @Test(timeout = 100000)
  public void testConnectWithDefaultUserPassAndCompression() throws Exception {
    checkConnectivity(true, "cassandra", "cassandra", Compression.LZ4, false);
    checkConnectivity(true, "cassandra", "cassandra", Compression.SNAPPY, false);
    checkConnectivity(true, "fakeUser", "fakePass", Compression.LZ4, true);
    checkConnectivity(true, "fakeUser", "fakePass", Compression.SNAPPY, true);
    checkConnectivity(false, null, null, Compression.LZ4, true);
    checkConnectivity(false, null, null, Compression.SNAPPY, true);
  }

  public void checkConnectivity(
      boolean usingAuth, String optUser, String optPass, boolean expectFailure) {
    checkConnectivity(usingAuth, optUser, optPass, Compression.NONE, expectFailure);
  }

  public void checkConnectivity(boolean usingAuth,
                                String optUser,
                                String optPass,
                                Compression compression,
                                boolean expectFailure) {
    // Use superclass definition to not have a default set of credentials.
    Cluster.Builder cb = super.getDefaultClusterBuilder();
    Cluster c = null;
    if (usingAuth) {
      cb = cb.withCredentials(optUser, optPass);
    }
    if (compression != Compression.NONE) {
      cb = cb.withCompression(compression);
    }
    c = cb.build();
    try {
      Session s = c.connect();
      s.execute("SELECT * FROM system_auth.roles;");
      // If we're expecting a failure, we should NOT be in here
      assertFalse(expectFailure);
    } catch (com.datastax.driver.core.exceptions.AuthenticationException e) {
      // If we're expecting a failure, we should be in here
      assertTrue(expectFailure);
    }
  }
}

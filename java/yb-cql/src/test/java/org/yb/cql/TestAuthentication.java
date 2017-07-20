// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.*;

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

  public void checkConnectivity(
      boolean usingAuth, String optUser, String optPass, boolean expectFailure) {
    // Use superclass definition to not have a default set of credentials.
    Cluster.Builder cb = super.getDefaultClusterBuilder();
    Cluster c = null;
    if (usingAuth) {
      cb = cb.withCredentials(optUser, optPass);
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

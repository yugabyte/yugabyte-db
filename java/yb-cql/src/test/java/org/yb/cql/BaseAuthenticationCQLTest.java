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
import org.junit.BeforeClass;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class BaseAuthenticationCQLTest extends BaseCQLTest {
  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    // Setting verbose level for debugging.
    BaseCQLTest.tserverArgs = Arrays.asList("--use_cassandra_authentication=true");
    BaseCQLTest.setUpBeforeClass();
  }

  public Cluster.Builder getDefaultClusterBuilder() {
    // Default return cassandra/cassandra auth.
    return super.getDefaultClusterBuilder().withCredentials("cassandra", "cassandra");
  }

  public Session getDefaultSession() {
    Cluster.Builder cb = getDefaultClusterBuilder();
    Cluster c = cb.build();
    Session s = c.connect();
    return s;
  }

  public void checkConnectivity(
      boolean usingAuth, String optUser, String optPass, boolean expectFailure) {
    checkConnectivity(usingAuth, optUser, optPass, ProtocolOptions.Compression.NONE, expectFailure);
  }

  public void checkConnectivity(boolean usingAuth,
                                String optUser,
                                String optPass,
                                ProtocolOptions.Compression compression,
                                boolean expectFailure) {
    // Use superclass definition to not have a default set of credentials.
    Cluster.Builder cb = super.getDefaultClusterBuilder();
    Cluster c = null;
    if (usingAuth) {
      cb = cb.withCredentials(optUser, optPass);
    }
    if (compression != ProtocolOptions.Compression.NONE) {
      cb = cb.withCompression(compression);
    }
    c = cb.build();
    try {
      Session s = c.connect();
      s.execute("SELECT * FROM system_auth.roles;");
      // If we're expecting a failure, we should NOT be in here.
      assertFalse(expectFailure);
    } catch (com.datastax.driver.core.exceptions.AuthenticationException e) {
      // If we're expecting a failure, we should be in here.
      assertTrue(expectFailure);
    }
  }

  // Verifies that roleName exists in the system_auth.roles table, and that canLogin and isSuperuser
  // match the fields 'can_login' and 'is_superuser' for this role.
  public void verifyRole(String roleName, boolean canLogin, boolean isSuperuser)
      throws Exception {
    verifyRoleFields(roleName, canLogin, isSuperuser, new ArrayList<>());
  }

  private void verifyRoleFields(String roleName, boolean canLogin, boolean isSuperuser,
                                List<String> memberOf) throws Exception {
    Session s = getDefaultSession();
    ResultSet rs = s.execute(
        String.format("SELECT * FROM system_auth.roles WHERE role = '%s';", roleName));

    Iterator<Row> iter = rs.iterator();
    assertTrue(String.format("Unable to find role '%s'", roleName), iter.hasNext());
    Row r = iter.next();
    assertEquals(r.getBool("can_login"), canLogin);
    assertEquals(r.getBool("is_superuser"), isSuperuser);
    assertEquals(r.getList("member_of", String.class), memberOf);
  }

  protected void testCreateRoleHelper(String roleName, String password, boolean canLogin,
                                      boolean isSuperuser) throws Exception {
    Session s = getDefaultSession();

    // Create the role.
    String createStmt = String.format(
        "CREATE ROLE %s WITH PASSWORD = '%s' AND LOGIN = %s AND SUPERUSER = %s",
        roleName, password, canLogin, isSuperuser);

    s.execute(createStmt);

    // Verify that we can connect using the new role.
    checkConnectivity(true, roleName, password, !canLogin);

    // Verify that the information got written into system_auth.roles correctly.
    verifyRole(roleName, canLogin, isSuperuser);
  }
}

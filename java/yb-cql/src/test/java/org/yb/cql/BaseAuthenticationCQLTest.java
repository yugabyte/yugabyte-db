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

import static junit.framework.TestCase.fail;
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

  public Session getSession(String username, String password) {
    Cluster.Builder cb = super.getDefaultClusterBuilder().withCredentials(username, password);
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
      s.execute("SELECT * FROM system_schema.tables;");
      // If we're expecting a failure, we should NOT be in here.
      assertFalse(expectFailure);
    } catch (com.datastax.driver.core.exceptions.AuthenticationException e) {
      // If we're expecting a failure, we should be in here.
      assertTrue(expectFailure);
    }
    c.close();
  }

  // Verifies that roleName exists in the system_auth.roles table, and that canLogin and isSuperuser
  // match the fields 'can_login' and 'is_superuser' for this role.
  public void verifyRole(String roleName, boolean canLogin, boolean isSuperuser)
      throws Exception {
    verifyRoleFields(roleName, canLogin, isSuperuser, new ArrayList<>());
  }

  public void verifyRoleWithSession(String roleName, boolean canLogin, boolean isSuperuser,
                                    Session s) throws Exception {
    verifyRoleFieldsWithSession(roleName, canLogin, isSuperuser, new ArrayList<>(), s);
  }

  private void verifyRoleFields(String roleName, boolean canLogin, boolean isSuperuser,
                                List<String> memberOf) throws Exception {
    Session s = getDefaultSession();
    verifyRoleFieldsWithSession(roleName, canLogin, isSuperuser, memberOf, s);
  }

  private void verifyRoleFieldsWithSession(String roleName, boolean canLogin, boolean isSuperuser,
                                          List<String> memberOf, Session s) throws Exception {
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
    testCreateRoleHelperWithSession(roleName, password, canLogin, isSuperuser, true, s);
  }

  protected void testCreateRoleHelperWithSession(String roleName, String password,
                                                 boolean canLogin, boolean isSuperuser,
                                                 boolean verifyConnectivity, Session s)
      throws Exception {

    // Create the role.
    String createStmt = String.format(
        "CREATE ROLE %s WITH PASSWORD = '%s' AND LOGIN = %s AND SUPERUSER = %s",
        roleName, password, canLogin, isSuperuser);

    s.execute(createStmt);

    // Verify that we can connect using the new role.
    if (verifyConnectivity) {
      checkConnectivity(true, roleName, password, !canLogin);
    }

    // Verify that the information got written into system_auth.roles correctly.
    verifyRoleWithSession(roleName, canLogin, isSuperuser, s);
  }

  public void assertPermissionsGranted(Session s, String role, String resource,
                                       List<String> permissions) {
    String stmt = String.format("SELECT permissions FROM system_auth.role_permissions " +
        "WHERE role = '%s' and resource = '%s';", role, resource);
    List<Row> rows = s.execute(stmt).all();
    assertEquals(1, rows.size());

    List list = rows.get(0).getList("permissions", String.class);
    assertEquals(permissions.size(), list.size());

    for (String permission : permissions) {
      if (!list.contains(permission)) {
        fail("Unable to find permission " + permission);
      }
    }
  }
}

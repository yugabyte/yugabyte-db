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

import static org.yb.AssertionWrappers.*;

import com.datastax.driver.core.*;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public abstract class BaseAuthenticationCQLTest extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseAuthenticationCQLTest.class);

  // Type of resources.
  public static final String ALL_KEYSPACES = "ALL KEYSPACES";
  public static final String KEYSPACE = "KEYSPACE";
  public static final String TABLE = "TABLE";
  public static final String ALL_ROLES = "ALL ROLES";
  public static final String ROLE = "ROLE";

  // Permissions.
  public static final String ALL = "ALL";
  public static final String ALTER = "ALTER";
  public static final String AUTHORIZE = "AUTHORIZE";
  public static final String CREATE = "CREATE";
  public static final String DESCRIBE = "DESCRIBE";
  public static final String DROP = "DROP";
  public static final String MODIFY = "MODIFY";
  public static final String SELECT = "SELECT";

  // Permissions in the same order as in catalog_manager.cc.
  public static final List<String> ALL_PERMISSIONS =
      Arrays.asList(ALTER, AUTHORIZE, CREATE, DESCRIBE, DROP, MODIFY, SELECT);

  public static final List<String> ALL_PERMISSIONS_FOR_KEYSPACE =
      Arrays.asList(ALTER, AUTHORIZE, CREATE, DROP, MODIFY, SELECT);

  public static final List<String> ALL_PERMISSIONS_FOR_TABLE =
      Arrays.asList(ALTER, AUTHORIZE, DROP, MODIFY, SELECT);

  public static final List<String> ALL_PERMISSIONS_FOR_ROLE =
      Arrays.asList(ALTER, AUTHORIZE, DROP);

  public static final List<String> ALL_PERMISSIONS_FOR_ALL_ROLES =
      Arrays.asList(ALTER, AUTHORIZE, CREATE, DESCRIBE, DROP);

  public static final List<String> ALL_PERMISSIONS_FOR_ALL_KEYSPACES =
      Arrays.asList(ALTER, AUTHORIZE, CREATE, DROP, MODIFY, SELECT);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_cassandra_authentication", "true");
    flagMap.put("password_hash_cache_size", "0");
    return flagMap;
  }

  @Override
  public Cluster.Builder getDefaultClusterBuilder() {
    // Default return cassandra/cassandra auth.
    return super.getDefaultClusterBuilder().withCredentials("cassandra", "cassandra");
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
    checkConnectivityWithMessage(usingAuth, optUser, optPass, compression, expectFailure, "");
  }

  public void checkConnectivityWithMessage(boolean usingAuth,
                                           String optUser,
                                           String optPass,
                                           ProtocolOptions.Compression compression,
                                           boolean expectFailure,
                                           String expectedMessage) {
    // Use superclass definition to not have a default set of credentials.
    Cluster.Builder cb = super.getDefaultClusterBuilder();
    if (usingAuth) {
      cb = cb.withCredentials(optUser, optPass);
    }
    if (compression != ProtocolOptions.Compression.NONE) {
      cb = cb.withCompression(compression);
    }
    try (Cluster c = cb.build()) {
      try (Session s = c.connect()) {
        s.execute("SELECT * FROM system_schema.tables");
      }
      // If we're expecting a failure, we should NOT be in here.
      assertFalse(expectFailure);
    } catch (com.datastax.driver.core.exceptions.AuthenticationException e) {
      // If we're expecting a failure, we should be in here.
      assertTrue(e.getMessage(), expectFailure);
      if (!e.getMessage().contains(expectedMessage)) {
        LOG.info("Expecting '" + expectedMessage + "' contained in '" + e.getMessage() + "'");
        assertTrue(false);
      }
    }
  }

  /**
   * Verifies that (lowercased) roleName exists in the system_auth.roles table, and that canLogin
   * and isSuperuser match the fields 'can_login' and 'is_superuser' for this role.
   */
  public void verifyRole(Session s, String roleName,
                         boolean canLogin, boolean isSuperuser) throws Exception {
    verifyRoleFields(s, roleName, canLogin, isSuperuser, new ArrayList<>());
  }

  private void verifyRoleFields(Session s, String roleName,
                                boolean canLogin, boolean isSuperuser,
                                List<String> memberOf) throws Exception {
    ResultSet rs = s.execute(
        String.format("SELECT * FROM system_auth.roles WHERE role = '%s';",
            roleName.toLowerCase()));
    List<Row> rows = rs.all();
    assertEquals(String.format("Unable to find role '%s'", roleName), 1, rows.size());
    Row r = rows.get(0);
    assertEquals(canLogin, r.getBool("can_login"));
    assertEquals(isSuperuser, r.getBool("is_superuser"));
    assertEquals(memberOf, r.getList("member_of", String.class));
  }

  /** Note that the role created will have lowercase name. */
  protected void createRole(Session s, String roleName, String password,
                            boolean canLogin, boolean isSuperuser,
                            boolean verifyConnectivity)
      throws Exception {

    // Create the role.
    s.execute(String.format(
        "CREATE ROLE %s WITH PASSWORD = '%s' AND LOGIN = %s AND SUPERUSER = %s",
        roleName, password, canLogin, isSuperuser));

    // Verify that we can connect using the new role.
    if (verifyConnectivity) {
      checkConnectivity(true, roleName, password, !canLogin);
    }

    // Verify that the information got written into system_auth.roles correctly.
    verifyRole(s, roleName, canLogin, isSuperuser);
  }

  public void assertPermissionsGranted(Session s, String role, String resource,
                                       List<String> permissions) {
    String stmt = String.format("SELECT permissions FROM system_auth.role_permissions " +
        "WHERE role = '%s' and resource = '%s';", role, resource);
    List<Row> rows = s.execute(stmt).all();

    if (permissions.isEmpty()) {
      assertEquals(0, rows.size());
      return;
    }

    assertEquals(1, rows.size());

    List<String> list = rows.get(0).getList("permissions", String.class);
    assertEquals(permissions.size(), list.size());

    for (String permission : permissions) {
      if (!list.contains(permission)) {
        fail("Unable to find permission " + permission);
      }
    }
  }

  public void grantPermission(Session s, String permission, String resourceType, String resource,
                              String role) throws Exception {
    s.execute(String.format("GRANT %s ON %s %s TO %s", permission, resourceType, resource, role));
  }
}

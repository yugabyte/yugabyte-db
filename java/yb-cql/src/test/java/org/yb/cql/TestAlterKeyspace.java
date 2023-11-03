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

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import com.datastax.driver.core.Session;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.RandomUtil;

@RunWith(value=YBTestRunner.class)
public class TestAlterKeyspace extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestAlterKeyspace.class);

  // We use a random keyspace name in each test to be sure that the keyspace does not exist in the
  // beginning of the test.
  private static String getRandomKeyspaceName() {
    return "test_keyspace_" + RandomUtil.randomNonNegNumber();
  }

  private String alterKeyspaceStmt(String keyspaceName, String options) throws Exception {
    return "ALTER KEYSPACE \"" + keyspaceName + "\"" + options;
  }

  private String durableWritesStmt(String value) {
    return String.format("REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 } " +
            "AND DURABLE_WRITES = %s;", value);
  }

  public void alterKeyspace(String keyspaceName, String options) throws Exception {
    execute(alterKeyspaceStmt(keyspaceName, options));
  }

  private void runInvalidAlterKeyspace(String keyspaceName, String options) throws Exception {
    executeInvalid(alterKeyspaceStmt(keyspaceName, options));
  }

  private void runInvalidKeyspaceProperty(String property) throws Exception {
    runInvalidStmt(alterKeyspaceStmt(DEFAULT_TEST_KEYSPACE, " WITH " + property));
  }

  private void runValidKeyspaceProperty(String property) throws Exception {
    session.execute(alterKeyspaceStmt(DEFAULT_TEST_KEYSPACE, " WITH " + property));
  }

  // Type of resources.
  private static final String ALL_KEYSPACES = "ALL KEYSPACES";
  private static final String KEYSPACE = "KEYSPACE";

  // Permissions.
  private static final String ALL = "ALL";
  private static final String ALTER = "ALTER";

  private void grantPermission(String permission, String resourceType, String resource,
                               String role) throws Exception {
    execute(String.format("GRANT %s ON %s %s TO %s", permission, resourceType, resource, role));
  }

  private ClusterAndSession createRoleAndLogin(String permission, String resourceType,
      String resource) throws Exception {
    int suffix = RandomUtil.randomNonNegNumber();
    final String username = "test_role_" + suffix;
    final String password = "test_role_password_" + suffix;

    // Create new user and grant permission.
    createRole(session, username, password, true, false, true);
    grantPermission(permission, resourceType, resource, username);
    LOG.info("Starting session with ROLE " + username);
    return connectWithCredentials(username, password);
  }

  @Test
  public void testSimpleAlterKeyspace() throws Exception {
    LOG.info("--- TEST CQL: SIMPLE ALTER KEYSPACE - Start");
    final String keyspaceName = getRandomKeyspaceName();

    // Non-existing keyspace.
    runInvalidAlterKeyspace(keyspaceName, "");

    createKeyspace(keyspaceName);

    // Alter created keyspace.
    alterKeyspace(keyspaceName, "");
    alterKeyspace(keyspaceName,
        " WITH REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");

    // Unknown strategy.
    runInvalidAlterKeyspace(keyspaceName,
        " WITH REPLICATION = { 'class' : 'MyStrategy', 'replication_factor' : 3 }");

    dropKeyspace(keyspaceName);
    LOG.info("--- TEST CQL: SIMPLE ALTER KEYSPACE - End");
  }

  @Test
  public void testDurableWrites() throws Exception {
    LOG.info("--- TEST CQL: DURABLE WRITES - Start");
    // Invalid cases.
    runInvalidKeyspaceProperty("DURABLE_WRITES = true");
    runInvalidKeyspaceProperty("DURABLE_WRITES = 'true'");
    runInvalidKeyspaceProperty("DURABLE_WRITES = false");
    runInvalidKeyspaceProperty("DURABLE_WRITES = 'false'");
    runInvalidKeyspaceProperty(durableWritesStmt("1"));
    runInvalidKeyspaceProperty(durableWritesStmt("1.0"));
    runInvalidKeyspaceProperty(durableWritesStmt("'a'"));
    runInvalidKeyspaceProperty(durableWritesStmt("{'a' : 1}"));
    runInvalidKeyspaceProperty(durableWritesStmt("{}"));

    // Valid cases.
    runValidKeyspaceProperty(durableWritesStmt("true"));
    runValidKeyspaceProperty(durableWritesStmt("'true'"));
    runValidKeyspaceProperty(durableWritesStmt("false"));
    runValidKeyspaceProperty(durableWritesStmt("'false'"));

    runValidKeyspaceProperty(durableWritesStmt("TRUE"));
    runValidKeyspaceProperty(durableWritesStmt("'TRUE'"));
    runValidKeyspaceProperty(durableWritesStmt("FALSE"));
    runValidKeyspaceProperty(durableWritesStmt("'FALSE'"));
    LOG.info("--- TEST CQL: DURABLE WRITES - End");
  }

  @Test
  public void testReplication() throws Exception {
    LOG.info("--- TEST CQL: REPLICATION - Start");
    // Invalid cases.
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : true }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 1}");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 1.0 }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : {} }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'a' }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'a', 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'SimpleStrategy' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 'a' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 'a' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3.0 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', " +
            "'replication_factor' : 9223372036854775808e9223372036854775808}");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3, 'dc1' : 1 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'd' : 1, 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : true }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 9223372036854775808 }");

    // Valid cases.
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : '3' }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : 3, 'dc2' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : '3' }");
    LOG.info("--- TEST CQL: REPLICATION - End");
  }

  @Test
  public void testAlterKeyspaceWithPermissions() throws Exception {
    LOG.info("--- TEST CQL: ALTER KEYSPACE WITH PERMISSIONS - Start");
    final String keyspaceName = getRandomKeyspaceName();

    createKeyspace(keyspaceName);

    final String alterStmt = alterKeyspaceStmt(keyspaceName,
            " WITH REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");

    // Alter created keyspace (default 'cassandra' superuser role).
    execute(alterStmt, session);

    // Alter keyspace WITHOUT correct permissions.
    createRole(session, "test_role", "test_password", true, false, true);
    try (ClusterAndSession cs = connectWithCredentials("test_role", "test_password")) {
      executeInvalid(alterStmt, cs.getSession());
    }

    // Permission ALL on ALL KEYSPACES.
    try (ClusterAndSession cs = createRoleAndLogin(ALL, ALL_KEYSPACES, "")) {
      execute(alterStmt, cs.getSession());
    }

    // Permission ALTER on ALL KEYSPACES.
    try (ClusterAndSession cs = createRoleAndLogin(ALTER, ALL_KEYSPACES, "")) {
      execute(alterStmt, cs.getSession());
      executeInvalid("DROP KEYSPACE \"" + keyspaceName + "\"", cs.getSession());
    }

    // Permission ALL on this keyspace.
    try (ClusterAndSession cs = createRoleAndLogin(ALL, KEYSPACE, keyspaceName)) {
      execute(alterStmt, cs.getSession());
      executeInvalid("DROP KEYSPACE \"" + DEFAULT_TEST_KEYSPACE + "\"", cs.getSession());
    }

    // Permission ALTER on this keyspace.
    try (ClusterAndSession cs = createRoleAndLogin(ALTER, KEYSPACE, keyspaceName)) {
      execute(alterStmt, cs.getSession());
      executeInvalid("DROP KEYSPACE \"" + DEFAULT_TEST_KEYSPACE + "\"", cs.getSession());
      executeInvalid("DROP KEYSPACE \"" + keyspaceName + "\"", cs.getSession());
    }

    dropKeyspace(keyspaceName);
    LOG.info("--- TEST CQL: ALTER KEYSPACE WITH PERMISSIONS - End");
  }
}

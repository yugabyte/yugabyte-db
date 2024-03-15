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
package org.yb.pgsql;


import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.sql.Connection;
import java.sql.Statement;

import org.apache.commons.lang3.StringUtils;

import org.apache.directory.server.annotations.CreateLdapServer;
import org.apache.directory.server.annotations.CreateTransport;
import org.apache.directory.server.core.annotations.ApplyLdifs;
import org.apache.directory.server.core.annotations.CreateDS;
import org.apache.directory.server.core.annotations.CreatePartition;
import org.apache.directory.server.core.integ.CreateLdapServerRule;
import org.junit.ClassRule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SystemUtil;
import org.yb.YBParameterizedTestRunner;
import com.yugabyte.util.PSQLException;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBParameterizedTestRunner.class)
@CreateDS(name = "myDS",
    partitions = {
        @CreatePartition(name = "test", suffix = "dc=myorg,dc=com")
    })
@CreateLdapServer(transports = {
  @CreateTransport(protocol = "LDAP", address = "localhost", port=10389)},
  allowAnonymousAccess = true)
@ApplyLdifs({
  "dn: dc=myorg,dc=com",
  "objectClass: domain",
  "objectClass: top",
  "dc: myorg",
  "dn: ou=Users,dc=myorg,dc=com",
  "objectClass: organizationalUnit",
  "objectClass: top",
  "ou: Users",
  "dn: cn=admin,ou=Users,dc=myorg,dc=com",
  "objectClass: inetOrgPerson",
  "objectClass: organizationalPerson",
  "objectClass: person",
  "objectClass: top",
  "cn: admin",
  "sn: Ldap",
  "uid: adminUid",
  "userPassword: adminPasswd",
  "dn: cn=testuser1,ou=Users,dc=myorg,dc=com",
  "objectClass: inetOrgPerson",
  "objectClass: organizationalPerson",
  "objectClass: person",
  "objectClass: top",
  "cn: testuser1",
  "sn: Ldap",
  "uid: testuser1Uid",
  "userPassword: 12345",
  "dn: cn=testUserNonUnique,ou=Users,dc=myorg,dc=com",
  "objectClass: inetOrgPerson",
  "objectClass: organizationalPerson",
  "objectClass: person",
  "objectClass: top",
  "cn: testUserNonUnique",
  "sn: Ldap",
  "uid: testUserNonUniqueUid",
  "userPassword: 12345",
  "dn: cn=testUserNonUnique,dc=myorg,dc=com",
  "objectClass: inetOrgPerson",
  "objectClass: organizationalPerson",
  "objectClass: person",
  "objectClass: top",
  "cn: testUserNonUnique",
  "sn: Ldap",
  "uid: testUserNonUniqueUid",
  "userPassword: 12345",
})
public class TestLDAPAuth extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestLDAPAuth.class);

  private static final String UNDEFINED_TSERVER_FLAG_ERROR_MSG =
      "expected env variable YSQL_LDAP_BIND_PWD_ENV to be defined";
  private static final String INCORRECT_PASSWORD_AUTH_MSG =
      "LDAP authentication failed for user";
  private static final String INCORRECT_ENV_PASSWORD_AUTH_MSG =
      "Check that the hostname and port are correct";
  private final ConnectionEndpoint connectionEndpoint;

  @Parameterized.Parameters
  public static List<ConnectionEndpoint> parameters() {
    if (SystemUtil.IS_LINUX)
      return Arrays.asList(ConnectionEndpoint.POSTGRES, ConnectionEndpoint.YSQL_CONN_MGR);
    else
      return Arrays.asList(ConnectionEndpoint.POSTGRES);
  }

  public TestLDAPAuth(ConnectionEndpoint connectionEndpoint) {
    this.connectionEndpoint = connectionEndpoint;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    if (connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR) {
      builder.enableYsqlConnMgr(true);
      builder.addCommonTServerFlag("ysql_conn_mgr_dowarmup", "false");
    }
  }

  @ClassRule
  public static CreateLdapServerRule serverRule = new CreateLdapServerRule();

  @Test
  public void searchBindModeWithSearchWithPlainTextPassword() throws Exception {
    // Tests regular case for search + bind LDAP mode.
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_hba_conf_csv",
                "\"host all testuser1 0.0.0.0/0 ldap ldapserver=localhost " +
                "ldapbasedn=\"\"ou=Users,dc=myorg,dc=com\"\" " +
                "ldapbinddn=\"\"cn=admin,ou=Users,dc=myorg,dc=com\"\" " +
                "ldapbindpasswd=\"\"adminPasswd\"\" ldapsearchattribute=\"\"cn\"\" " +
                "ldapport=10389\"," +
                "\"host all all 0.0.0.0/0 trust\"");
    LOG.info(flagMap.get("ysql_hba_conf_csv"));
    restartClusterWithFlags(
        Collections.emptyMap(),
        flagMap);

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // Basic LDAP login should work.
    try (Connection connection = passRoleUserConnBldr.withPassword("12345")
        .withConnectionEndpoint(connectionEndpoint).connect()) {
      // No-op.
    }

    // Basic LDAP login with incorrect password.
    try (Connection connection = passRoleUserConnBldr.withPassword("123")
        .withConnectionEndpoint(connectionEndpoint).connect()) {
      // No-op.
    } catch (PSQLException e) {
      if (StringUtils.containsIgnoreCase(e.getMessage(), INCORRECT_PASSWORD_AUTH_MSG)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
                           e.getMessage(), INCORRECT_PASSWORD_AUTH_MSG));
      }
    }
  }

  @Test
  public void searchBindModeWithSearchWithEnvVarDefined() throws Exception {
    // Tests LDAP search + bind password is properly specified via env variable.
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_hba_conf_csv",
                "\"host all testuser1 0.0.0.0/0 ldap ldapserver=localhost " +
                "ldapbasedn=\"\"ou=Users,dc=myorg,dc=com\"\" " +
                "ldapbinddn=\"\"cn=admin,ou=Users,dc=myorg,dc=com\"\" " +
                "ldapbindpasswd=\"\"YSQL_LDAP_BIND_PWD_ENV\"\" ldapsearchattribute=\"\"cn\"\" " +
                "ldapport=10389\"," +
                "\"host all all 0.0.0.0/0 trust\"");
    LOG.info(flagMap.get("ysql_hba_conf_csv"));
    restartClusterWithFlagsAndEnv(
        Collections.emptyMap(),
        flagMap,
        Collections.singletonMap(
          "YSQL_LDAP_BIND_PWD_ENV", "adminPasswd"));

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");

    // Basic LDAP login should work.
    try (Connection connection = passRoleUserConnBldr.withPassword("12345")
        .withConnectionEndpoint(connectionEndpoint).connect()) {
      // No-op.
    }

    // Tests LDAP search + bind password with wrong password specified to environment variable.
    restartClusterWithFlagsAndEnv(
        Collections.emptyMap(),
        flagMap,
        Collections.singletonMap(
        "YSQL_LDAP_BIND_PWD_ENV", "incorrectPasswd"));

    // Basic LDAP login should not work since admin password is incorrect.
    try (Connection connection = passRoleUserConnBldr.withPassword("12345")
        .withConnectionEndpoint(connectionEndpoint).connect()) {
      // No-op.
    } catch (PSQLException e) {
      if (StringUtils.containsIgnoreCase(e.getMessage(), INCORRECT_ENV_PASSWORD_AUTH_MSG)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
                           e.getMessage(), INCORRECT_ENV_PASSWORD_AUTH_MSG));
      }
    }
  }

  @Test
  public void searchBindModeWithSearchWithoutEnvVariable() throws Exception {
    // Verifies error is thrown if env variable is not defined.
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_hba_conf_csv",
                "\"host all testuser1 0.0.0.0/0 ldap ldapserver=localhost " +
                "ldapbasedn=\"\"ou=Users,dc=myorg,dc=com\"\" " +
                "ldapbinddn=\"\"cn=admin,ou=Users,dc=myorg,dc=com\"\" " +
                "ldapbindpasswd=\"\"YSQL_LDAP_BIND_PWD_ENV\"\" ldapsearchattribute=\"\"cn\"\" " +
                "ldapport=10389\"," +
                "\"host all all 0.0.0.0/0 trust\"");
    LOG.info(flagMap.get("ysql_hba_conf_csv"));
    restartClusterWithFlags(
        Collections.emptyMap(),
        flagMap);

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE testuser1 LOGIN");
    }

    ConnectionBuilder passRoleUserConnBldr = getConnectionBuilder().withUser("testuser1");
    // Basic LDAP login
    try (Connection connection = passRoleUserConnBldr.withPassword("12345")
        .withConnectionEndpoint(connectionEndpoint).connect()) {
      // No-op.
    } catch (PSQLException e) {
      if (StringUtils.containsIgnoreCase(e.getMessage(), UNDEFINED_TSERVER_FLAG_ERROR_MSG)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
                           e.getMessage(), UNDEFINED_TSERVER_FLAG_ERROR_MSG));
      }
    }
  }
}

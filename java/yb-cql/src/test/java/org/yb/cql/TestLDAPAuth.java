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


import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import com.datastax.driver.core.ProtocolOptions;

import org.apache.directory.server.annotations.CreateLdapServer;
import org.apache.directory.server.annotations.CreateTransport;
import org.apache.directory.server.core.annotations.ApplyLdifs;
import org.apache.directory.server.core.annotations.CreateDS;
import org.apache.directory.server.core.annotations.CreatePartition;
import org.apache.directory.server.core.integ.CreateLdapServerRule;
import org.junit.ClassRule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

// TODO(Piyush): Test with TLS and LDAPS
// TODO(Piyush): Reduce test time by force updating gflags instead of restarting cluster

@RunWith(value=YBTestRunner.class)
@CreateDS(name = "myDS",
    partitions = {
        @CreatePartition(name = "test", suffix = "dc=myorg,dc=com")
    })
@CreateLdapServer(transports = {
  @CreateTransport(protocol = "LDAP", address = "localhost", port=10389)},
  allowAnonymousAccess = true) // We allow this to test if YCQL still blocks such a bind
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
  "dn: cn=testUser1,ou=Users,dc=myorg,dc=com",
  "objectClass: inetOrgPerson",
  "objectClass: organizationalPerson",
  "objectClass: person",
  "objectClass: top",
  "cn: testUser1",
  "sn: Ldap",
  "uid: testUser1Uid",
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
public class TestLDAPAuth extends BaseAuthenticationCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestLDAPAuth.class);

  @ClassRule
  public static CreateLdapServerRule serverRule = new CreateLdapServerRule();

  @Override
  public int getTestMethodTimeoutSec() {
    return 240;
  }

  private void recreateMiniCluster(Map<String, String> extraTserverFlag) throws Exception {
    destroyMiniCluster();
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("use_cassandra_authentication", "true");
    tserverFlags.put("ycql_use_ldap", "true");
    tserverFlags.put("ycql_ldap_users_to_skip_csv", "cassandra");
    tserverFlags.put("ycql_ldap_server", "ldap://localhost:10389");
    tserverFlags.put("ycql_ldap_tls", "false");
    tserverFlags.put("vmodule", "cql_processor=4");
    tserverFlags.putAll(extraTserverFlag);
    createMiniCluster(Collections.emptyMap(), tserverFlags);
    setUpCqlClient();
  }

  @Test
  public void incorrectLDAPServer() throws Exception {
    Map<String, String> extraTserverFlagMap = getTServerFlags();
    extraTserverFlagMap.put("ycql_ldap_server", "ldap://localhost:1039");
    extraTserverFlagMap.put("ycql_ldap_user_prefix", "cn=");
    extraTserverFlagMap.put("ycql_ldap_user_suffix", ",ou=Users,dc=myorg,dc=com");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */, "Can't contact LDAP server");
    session.execute("DROP ROLE 'testUser1'");
  }

  void testUnauthenticatedAndAnonymousBinds() {
    // Test Unauthenticated bind - non-empty username and empty password
    // Refer: https://datatracker.ietf.org/doc/html/rfc4513#section-5.1.2
    checkConnectivityWithMessage(true, "testUser1", "", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Empty username and/or password not allowed");

    // In below cases, with empty user name, since the user doesn't exist in cassandra, we error out
    // at an earlier stage and hence the error is different from the one for empty password.

    // Test Anonymous bind - empty username and empty password
    // Refer: https://datatracker.ietf.org/doc/html/rfc4513#section-5.1.1
    checkConnectivityWithMessage(true, "", "", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Provided username '' and/or password are incorrect");

    // Extra case - empty username and non-empty password
    checkConnectivityWithMessage(true, "", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Provided username '' and/or password are incorrect");
  }

  @Test
  public void simpleBindMode() throws Exception {
    // Test with incorrect user prefix
    Map<String, String> extraTserverFlagMap = new HashMap<String, String>();
    extraTserverFlagMap.put("ycql_ldap_user_prefix", "dummy=");
    extraTserverFlagMap.put("ycql_ldap_user_suffix", ",ou=Users,dc=myorg,dc=com");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or password are " +
      "incorrect");

    testUnauthenticatedAndAnonymousBinds();

    // Test with incorrect user suffix
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_user_prefix", "cn=");
    extraTserverFlagMap.put("ycql_ldap_user_suffix", ",dc=myorg,dc=com");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or password are " +
      "incorrect");

    testUnauthenticatedAndAnonymousBinds();

    // Test with correct prefix and suffix
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_user_prefix", "cn=");
    extraTserverFlagMap.put("ycql_ldap_user_suffix", ",ou=Users,dc=myorg,dc=com");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      false /* expectFailure */, "");

    // Incorrect password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or password are " +
      "incorrect");

    // Incorrect user - no role is created for testUser2
    checkConnectivityWithMessage(true, "testUser2", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Provided username 'testUser2' and/or password are incorrect");

    testUnauthenticatedAndAnonymousBinds();

    session.execute("DROP ROLE 'testUser1'");
  }

  @Test
  public void searchBindModeWithSearchAttribute() throws Exception {
    // Test with incorrect bind user dn
    Map<String, String> extraTserverFlagMap = getTServerFlags();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=dummy,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "could not perform initial LDAP bind for ldapbinddn 'cn=dummy,ou=Users,dc=myorg,dc=com'");

    testUnauthenticatedAndAnonymousBinds();

    // Test with incorrect bind password
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "dummyPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "could not perform initial LDAP bind for ldapbinddn 'cn=admin,ou=Users,dc=myorg,dc=com'");

    testUnauthenticatedAndAnonymousBinds();

    // Test with non-existant base dn
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=dummy,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "could not search LDAP for filter '(cn=testUser1)' on server " +
      "'ldap://localhost:10389': No such object LDAP diagnostics: " +
      "NO_SUCH_OBJECT: failed for MessageType : SEARCH_REQUEST");

    testUnauthenticatedAndAnonymousBinds();

    // Test with incorrect search attribute
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "dummy");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "LDAP user 'testUser1' does not exist. LDAP search for filter '(dummy=testUser1)' on " +
      "server 'ldap://localhost:10389' returned no entries.");

    testUnauthenticatedAndAnonymousBinds();

    // Test with all correct - bind db, bind password, base dn, search attribute
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    // Test with incorrect user
    checkConnectivityWithMessage(true, "dummy", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Provided username 'dummy' and/or password are incorrect");

    // Test with incorrect password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or " +
      "password are incorrect");

    // Test with correct password
    checkConnectivity(true, "testUser1", "12345", false /* expectFailure */);

    testUnauthenticatedAndAnonymousBinds();

    // Test with prefix of base dn
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    // Test with incorrect user
    checkConnectivityWithMessage(true, "dummy", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Provided username 'dummy' and/or password are incorrect");

    // Test with incorrect password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or " +
      "password are incorrect");

    // Test with correct password
    checkConnectivity(true, "testUser1", "12345", false /* expectFailure */);

    testUnauthenticatedAndAnonymousBinds();

    session.execute("CREATE ROLE 'test*User1' WITH LOGIN = true");
    // Test with user name that has characters that are not allowed
    checkConnectivityWithMessage(true, "test*User1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "invalid character in user name for LDAP authentication");

    session.execute("CREATE ROLE 'testUserNonUnique' WITH LOGIN = true");
    // Test with more than one user name matching search criteria
    checkConnectivityWithMessage(true, "testUserNonUnique", "12345",
      ProtocolOptions.Compression.NONE, true, /* expectFailure */
      "LDAP user 'testUserNonUnique' is not unique, 2 entries exist.");
  }

  @Test
  public void searchBindModeWithEmptyConfigArgs() throws Exception {
    // Test search+bind mode with empty arguments for -
    //   1. ycql_ldap_bind_dn
    //   2. ycql_ldap_bind_passwd
    //   3. both ycql_ldap_bind_dn and ycql_ldap_bind_passwd empty
    Map<String, String> extraTserverFlagMap = getTServerFlags();

    // Test with empty bind dn (all other flags have correct values)
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    // Test with correct username and password
    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Empty bind dn and/or bind password not allowed for search+bind mode");

    // Test with empty bind password (all other flags have correct values)
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Empty bind dn and/or bind password not allowed for search+bind mode");

    // Test with both empty bind user dn and empty bind password (all other flags have correct
    // values)
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_attribute", "cn");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Empty bind dn and/or bind password not allowed for search+bind mode");
  }

  @Test
  public void searchBindModeWithSearchFilter() throws Exception {
    // Test with incorrect bind user dn
    Map<String, String> extraTserverFlagMap = getTServerFlags();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=dummy,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(cn=$username)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "could not perform initial LDAP bind for ldapbinddn 'cn=dummy,ou=Users,dc=myorg,dc=com'");

    testUnauthenticatedAndAnonymousBinds();

    // Test with incorrect bind password
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "dummyPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(cn=$username)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "could not perform initial LDAP bind for ldapbinddn 'cn=admin,ou=Users,dc=myorg,dc=com'");

    testUnauthenticatedAndAnonymousBinds();

    // Test with non-existant base dn
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=dummy,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(cn=$username)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "could not search LDAP for filter '(cn=testUser1)' on server " +
      "'ldap://localhost:10389': No such object LDAP diagnostics: " +
      "NO_SUCH_OBJECT: failed for MessageType : SEARCH_REQUEST");

    testUnauthenticatedAndAnonymousBinds();

    // Test with incorrect search filters
    // Case 1: Incorrect tag instead of $username
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(cn=$userna..)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "LDAP user 'testUser1' does not exist. LDAP search for filter '(cn=$userna..)' on " +
      "server 'ldap://localhost:10389' returned no entries.");

    testUnauthenticatedAndAnonymousBinds();

    // Case 2: Incorrect attribute used, correct $username tag
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(dummy=$username)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "LDAP user 'testUser1' does not exist. LDAP search for filter '(dummy=testUser1)' on " +
      "server 'ldap://localhost:10389' returned no entries.");

    testUnauthenticatedAndAnonymousBinds();

    // Test with all correct - bind db, bind password, base dn, search filter
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(cn=$username)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    // Test with incorrect user
    checkConnectivityWithMessage(true, "dummy", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Provided username 'dummy' and/or password are incorrect");

    // Test with incorrect password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or " +
      "password are incorrect");

    // Test with correct password
    checkConnectivity(true, "testUser1", "12345", false /* expectFailure */);

    testUnauthenticatedAndAnonymousBinds();

    // Test with prefix of base dn
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_bind_dn", "cn=admin,ou=Users,dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_bind_passwd", "adminPasswd");
    extraTserverFlagMap.put("ycql_ldap_base_dn", "dc=myorg,dc=com");
    extraTserverFlagMap.put("ycql_ldap_search_filter", "(cn=$username)");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    // Test with incorrect user
    checkConnectivityWithMessage(true, "dummy", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Provided username 'dummy' and/or password are incorrect");

    // Test with incorrect password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Failed to authenticate using LDAP: Provided username 'testUser1' and/or " +
      "password are incorrect");

    // Test with correct password
    checkConnectivity(true, "testUser1", "12345", false /* expectFailure */);

    testUnauthenticatedAndAnonymousBinds();

    session.execute("CREATE ROLE 'test*User1' WITH LOGIN = true");
    // Test with user name that has characters that are not allowed
    checkConnectivityWithMessage(true, "test*User1", "12345", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "invalid character in user name for LDAP authentication");

    session.execute("CREATE ROLE 'testUserNonUnique' WITH LOGIN = true");
    // Test with more than one user name matching search criteria
    checkConnectivityWithMessage(true, "testUserNonUnique", "12345",
      ProtocolOptions.Compression.NONE, true, /* expectFailure */
      "LDAP user 'testUserNonUnique' is not unique, 2 entries exist.");
  }
}

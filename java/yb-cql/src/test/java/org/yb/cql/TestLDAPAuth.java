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

@RunWith(value=YBTestRunner.class)
@CreateDS(name = "myDS",
    partitions = {
        @CreatePartition(name = "test", suffix = "dc=myorg,dc=com")
    })
@CreateLdapServer(transports = {
  @CreateTransport(protocol = "LDAP", address = "localhost", port=10389)})
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
      "Failed to authenticate using LDAP: Provided username testUser1 and/or password are " +
      "incorrect");

    // Test with incorrect user suffix
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_user_prefix", "cn=");
    extraTserverFlagMap.put("ycql_ldap_user_suffix", ",dc=myorg,dc=com");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Failed to authenticate using LDAP: Provided username testUser1 and/or password are " +
      "incorrect");

    // Test with correct prefix and suffix
    extraTserverFlagMap.clear();
    extraTserverFlagMap.put("ycql_ldap_user_prefix", "cn=");
    extraTserverFlagMap.put("ycql_ldap_user_suffix", ",ou=Users,dc=myorg,dc=com");
    recreateMiniCluster(extraTserverFlagMap);
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

    checkConnectivityWithMessage(true, "testUser1", "12345", ProtocolOptions.Compression.NONE,
      false /* expectFailure */, "");

    // Incorrect username/password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Failed to authenticate using LDAP: Provided username testUser1 and/or password are " +
      "incorrect");

    checkConnectivityWithMessage(true, "testUser2", "12345", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Provided username testUser2 and/or password are incorrect");

    checkConnectivityWithMessage(true, "testUser1", "", ProtocolOptions.Compression.NONE,
      true /* expectFailure */,
      "Failed to authenticate using LDAP: Internal error");

    session.execute("DROP ROLE 'testUser1'");
  }

  @Test
  public void searchBindMode() throws Exception {
    session.execute("CREATE ROLE 'testUser1' WITH LOGIN = true");

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

    // Test with incorrect incorrect search attribute
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
      "Provided username dummy and/or password are incorrect");

    // Test with incorrect user and password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Failed to authenticate using LDAP: Provided username testUser1 and/or " +
      "password are incorrect");

    // Test with correct password
    checkConnectivity(true, "testUser1", "12345", false /* expectFailure */);

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
      "Provided username dummy and/or password are incorrect");

    // Test with incorrect user password
    checkConnectivityWithMessage(true, "testUser1", "1234", ProtocolOptions.Compression.NONE,
      true, /* expectFailure */
      "Failed to authenticate using LDAP: Provided username testUser1 and/or " +
      "password are incorrect");

    // Test with correct password
    checkConnectivity(true, "testUser1", "12345", false /* expectFailure */);

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

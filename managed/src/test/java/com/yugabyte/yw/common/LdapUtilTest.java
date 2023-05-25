package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.AdditionalMatchers.not;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.LdapDnToYbaRole;
import com.yugabyte.yw.models.Users;
import org.apache.directory.api.ldap.model.cursor.EntryCursor;
import org.apache.directory.api.ldap.model.entry.DefaultEntry;
import org.apache.directory.api.ldap.model.entry.Entry;
import org.apache.directory.api.ldap.model.exception.LdapAuthenticationException;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.apache.directory.api.ldap.model.exception.LdapNoSuchObjectException;
import org.apache.directory.api.ldap.model.message.SearchScope;
import org.apache.directory.ldap.client.api.LdapNetworkConnection;
import org.junit.Before;
import org.junit.Test;
import org.mockito.MockitoAnnotations;

public class LdapUtilTest extends FakeDBApplication {

  private LdapUtil ldapUtil;
  private EntryCursor entryCursor;
  private LdapNetworkConnection ldapNetworkConnection;
  private Customer testCustomer;

  private final String memberAttribute = "member";
  private final String memberOfAttribute = "memberOf";
  private final String baseDn = "dc=base-dn";
  private final String dnPrefix = "uid=";
  private final String username = "test-user";

  @Before
  public void setup() {
    MockitoAnnotations.initMocks(this);
  }

  private void setupTest() throws Exception {
    ldapUtil = spy(LdapUtil.class);
    entryCursor = mock(EntryCursor.class);
    ldapNetworkConnection = mock(LdapNetworkConnection.class);
    testCustomer = ModelFactory.testCustomer();

    doReturn(ldapNetworkConnection).when(ldapUtil).createNewLdapConnection(any());
    doNothing().when(ldapNetworkConnection).bind(anyString(), anyString());
    doReturn(entryCursor)
        .when(ldapNetworkConnection)
        .search(
            anyString(),
            not(
                eq(
                    "(&(objectClass=*)("
                        + memberAttribute
                        + "="
                        + dnPrefix
                        + username
                        + ","
                        + baseDn
                        + "))")),
            any(),
            anyString());
  }

  @Test
  public void testAuthViaLDAPSimpleBindSuccess() throws Exception {
    setupTest();
    Entry entry = new DefaultEntry();
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(entry);

    Users user =
        ldapUtil.authViaLDAP(
            "test-user",
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                "base-dn",
                "",
                "",
                false,
                false,
                false,
                "",
                "",
                "",
                false,
                "*",
                SearchScope.SUBTREE,
                "",
                false,
                false));

    assertNotNull(user);
    assertEquals("test-user", user.getEmail());
  }

  @Test
  public void testAuthViaLDAPSimpleBindFailure() throws Exception {
    setupTest();
    doThrow(LdapAuthenticationException.class)
        .doNothing()
        .when(ldapNetworkConnection)
        .bind(anyString(), anyString());

    assertThrows(
        PlatformServiceException.class,
        () ->
            ldapUtil.authViaLDAP(
                "test-user",
                "password",
                new LdapUtil.LdapConfiguration(
                    "ldapUrl",
                    389,
                    "base-dn",
                    "",
                    "cn=",
                    false,
                    false,
                    false,
                    "",
                    "",
                    "",
                    false,
                    "*",
                    SearchScope.SUBTREE,
                    "",
                    false,
                    false)));
  }

  @Test
  public void testAuthViaLDAPSimpleBindWithUserDeleted() throws Exception {
    setupTest();
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(Users.UserType.ldap);
    user.update();
    doThrow(LdapNoSuchObjectException.class)
        .doNothing()
        .when(ldapNetworkConnection)
        .bind(anyString(), anyString());

    assertThrows(
        PlatformServiceException.class,
        () ->
            ldapUtil.authViaLDAP(
                user.getEmail(),
                "password",
                new LdapUtil.LdapConfiguration(
                    "ldapUrl",
                    389,
                    "base-dn",
                    "",
                    "cn=",
                    false,
                    false,
                    false,
                    "",
                    "",
                    "",
                    false,
                    "*",
                    SearchScope.SUBTREE,
                    "",
                    false,
                    false)));
    assertNull(Users.getByEmail(user.getEmail()));
  }

  @Test
  public void testAuthViaLDAPReadRole() throws Exception {
    setupTest();
    Entry entry = new DefaultEntry();
    entry.add("yugabytePlatformRole", "BackupAdmin");
    doNothing().when(ldapNetworkConnection).unBind();
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(entry);

    Users user =
        ldapUtil.authViaLDAP(
            "test-user",
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                "base-dn",
                "",
                "cn=",
                false,
                false,
                false,
                "service_account",
                "service_password",
                "",
                false,
                "*",
                SearchScope.SUBTREE,
                "",
                false,
                false));

    assertNotNull(user);
    assertEquals(Users.Role.BackupAdmin, user.getRole());
  }

  @Test
  public void testAuthViaLDAPUpdateRole() throws Exception {
    setupTest();
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setLdapSpecifiedRole(true);
    user.setUserType(Users.UserType.ldap);
    user.update();
    Entry entry = new DefaultEntry();
    entry.add("yugabytePlatformRole", "BackupAdmin");
    doNothing().when(ldapNetworkConnection).unBind();
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(entry);

    Users updatedUser =
        ldapUtil.authViaLDAP(
            user.getEmail(),
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                "base-dn",
                "",
                "cn=",
                false,
                false,
                false,
                "service_account",
                "service_password",
                "",
                false,
                "*",
                SearchScope.SUBTREE,
                "",
                false,
                false));

    assertNotNull(updatedUser);
    assertEquals(Users.Role.BackupAdmin, updatedUser.getRole());
  }

  @Test
  public void testAuthViaLDAPWithOldUser() throws Exception {
    setupTest();
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setLdapSpecifiedRole(true);
    user.setUserType(Users.UserType.ldap);
    user.update();
    Entry entry = new DefaultEntry();
    entry.add("yugabytePlatformRole", "Admin");
    doNothing().when(ldapNetworkConnection).unBind();
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(entry);

    Users oldUser =
        ldapUtil.authViaLDAP(
            user.getEmail(),
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                "base-dn",
                "",
                "cn=",
                false,
                false,
                false,
                "service_account",
                "service_password",
                "",
                false,
                "*",
                SearchScope.SUBTREE,
                "",
                false,
                false));

    assertEquals(user, oldUser);
  }

  @Test
  public void testAuthViaLDAPWithSearchAndBindSuccess() throws Exception {
    setupTest();
    Entry entry = new DefaultEntry("CN=" + username);
    entry.add("yugabytePlatformRole", "Admin");
    doNothing().when(ldapNetworkConnection).unBind();
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(entry);

    Users user =
        ldapUtil.authViaLDAP(
            "test-user",
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                "base-dn",
                "",
                "cn=",
                false,
                false,
                true,
                "service_account",
                "service_password",
                "search-attribute",
                false,
                "*",
                SearchScope.SUBTREE,
                "",
                false,
                false));

    assertNotNull(user);
    assertEquals("test-user", user.getEmail());
  }

  @Test
  public void testAuthViaLDAPWithSearchAndBindWithoutServiceAccountAndSearchAttribute()
      throws Exception {
    setupTest();
    assertThrows(
        PlatformServiceException.class,
        () ->
            ldapUtil.authViaLDAP(
                "test-user",
                "password",
                new LdapUtil.LdapConfiguration(
                    "ldapUrl",
                    389,
                    "base-dn",
                    "",
                    "cn=",
                    false,
                    false,
                    true,
                    "",
                    "",
                    "",
                    false,
                    "*",
                    SearchScope.SUBTREE,
                    "",
                    false,
                    false)));
  }

  @Test
  public void testAuthViaLDAPGroupSearchFilter() throws Exception {
    setupTest();

    String dn = dnPrefix + username + "," + baseDn;

    Entry userEntry = new DefaultEntry(dn);
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(userEntry);

    String groupDn = "cn=mygroup," + baseDn;

    LdapDnToYbaRole ldapMapping =
        LdapDnToYbaRole.create(testCustomer.getUuid(), groupDn, Users.Role.Admin);

    Entry groupEntry = new DefaultEntry(groupDn);
    groupEntry.add(memberAttribute, dn);

    EntryCursor groupEntryCursor = mock(EntryCursor.class);
    when(groupEntryCursor.next()).thenReturn(true).thenReturn(false);
    when(groupEntryCursor.get()).thenReturn(groupEntry);

    doReturn(groupEntryCursor)
        .when(ldapNetworkConnection)
        .search(
            anyString(),
            eq(
                "(&(objectClass=*)("
                    + memberAttribute
                    + "="
                    + dnPrefix
                    + username
                    + ","
                    + baseDn
                    + "))"),
            any(),
            anyString());

    Users user =
        ldapUtil.authViaLDAP(
            username,
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                baseDn,
                "",
                dnPrefix,
                false,
                false,
                false,
                "username",
                "password",
                "",
                false,
                "(&(objectClass=*)("
                    + memberAttribute
                    + "="
                    + dnPrefix
                    + LdapUtil.USERNAME_KEYWORD
                    + ","
                    + baseDn
                    + "))",
                SearchScope.SUBTREE,
                "",
                true,
                true));

    assertNotNull(user);
    assertEquals(username, user.getEmail());
    assertEquals(user.getRole(), ldapMapping.ybaRole);
  }

  @Test
  public void testAuthViaLDAPUserMemberOfAttribute() throws Exception {
    setupTest();

    String dn = dnPrefix + username + "," + baseDn;

    String groupDn = "cn=groupname," + baseDn;

    Entry userEntry = new DefaultEntry(dn);
    userEntry.add(memberOfAttribute, groupDn);
    when(entryCursor.next()).thenReturn(true).thenReturn(false);
    when(entryCursor.get()).thenReturn(userEntry);

    LdapDnToYbaRole ldapMapping =
        LdapDnToYbaRole.create(testCustomer.getUuid(), groupDn, Users.Role.Admin);

    Users user =
        ldapUtil.authViaLDAP(
            username,
            "password",
            new LdapUtil.LdapConfiguration(
                "ldapUrl",
                389,
                baseDn,
                "",
                dnPrefix,
                false,
                false,
                false,
                "username",
                "password",
                "",
                false,
                "",
                null,
                memberOfAttribute,
                false,
                true));

    assertNotNull(user);
    assertEquals(username, user.getEmail());
    assertEquals(user.getRole(), ldapMapping.ybaRole);
  }

  @Test
  public void testAuthWithLDAPRoleMappingServiceAccountRequired() throws Exception {
    setupTest();

    assertPlatformException(
        () -> {
          try {
            ldapUtil.authViaLDAP(
                username,
                "password",
                new LdapUtil.LdapConfiguration(
                    "ldapUrl",
                    389,
                    baseDn,
                    "",
                    dnPrefix,
                    false,
                    false,
                    false,
                    "",
                    "",
                    "",
                    false,
                    "",
                    null,
                    memberOfAttribute,
                    false,
                    true));
          } catch (LdapException e) {
            throw new RuntimeException(e);
          }
        });
    ;
  }
}

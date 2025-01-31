package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.UNAUTHORIZED;

import com.google.inject.Inject;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import io.ebean.DB;
import io.ebean.DuplicateKeyException;
import java.nio.charset.Charset;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutableTriple;
import org.apache.commons.lang3.tuple.Triple;
import org.apache.directory.api.ldap.model.cursor.CursorException;
import org.apache.directory.api.ldap.model.cursor.EntryCursor;
import org.apache.directory.api.ldap.model.entry.Attribute;
import org.apache.directory.api.ldap.model.entry.Entry;
import org.apache.directory.api.ldap.model.entry.Value;
import org.apache.directory.api.ldap.model.exception.LdapAuthenticationException;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.apache.directory.api.ldap.model.exception.LdapNoSuchObjectException;
import org.apache.directory.api.ldap.model.message.SearchScope;
import org.apache.directory.ldap.client.api.LdapConnectionConfig;
import org.apache.directory.ldap.client.api.LdapNetworkConnection;
import org.apache.directory.ldap.client.api.NoVerificationTrustManager;
import org.apache.directory.ldap.client.api.exception.LdapConnectionTimeOutException;

@Slf4j
public class LdapUtil {
  public static final String windowsAdUserDoesNotExistErrorCode = "data 2030";
  public static final String USERNAME_KEYWORD = "{username}";

  @Inject private RuntimeConfGetter confGetter;

  @Inject private CustomCAStoreManager customCAStoreManager;

  @Getter
  @Setter
  @AllArgsConstructor
  public static class LdapConfiguration {
    String ldapUrl;
    Integer ldapPort;
    String ldapBaseDN;
    String ldapCustomerUUID;
    String ldapDnPrefix;
    boolean ldapUseSsl;
    boolean ldapUseTls;
    boolean useLdapSearchAndBind;
    String serviceAccountDistinguishedName;
    String serviceAccountPassword;
    String ldapSearchAttribute;
    String ldapSearchFilter;
    boolean enableDetailedLogs;
    String ldapGroupSearchFilter;
    SearchScope ldapGroupSearchScope;
    String ldapGroupSearchBaseDn;
    String ldapGroupMemberOfAttribute;
    boolean ldapGroupUseQuery;
    boolean ldapGroupUseRoleMapping;
    Role ldapDefaultRole;
    TlsProtocol ldapTlsProtocol;
    boolean useNewRbacAuthz;
  }

  public enum TlsProtocol {
    TLSv1,
    TLSv1_1,
    TLSv1_2;

    public String getVersionString() {
      return this.toString().replace('_', '.');
    }
  }

  public Users loginWithLdap(CustomerLoginFormData data) throws LdapException {
    String ldapUrl = confGetter.getGlobalConf(GlobalConfKeys.ldapUrl);
    String getLdapPort = confGetter.getGlobalConf(GlobalConfKeys.ldapPort);
    Integer ldapPort = Integer.parseInt(getLdapPort);
    String ldapBaseDN = confGetter.getGlobalConf(GlobalConfKeys.ldapBaseDn);
    String ldapCustomerUUID = confGetter.getGlobalConf(GlobalConfKeys.ldapCustomerUUID);
    String ldapDnPrefix = confGetter.getGlobalConf(GlobalConfKeys.ldapDnPrefix);
    boolean ldapUseSsl = confGetter.getGlobalConf(GlobalConfKeys.enableLdap);
    boolean ldapUseTls = confGetter.getGlobalConf(GlobalConfKeys.enableLdapStartTls);
    boolean useLdapSearchAndBind = confGetter.getGlobalConf(GlobalConfKeys.ldapUseSearchAndBind);
    String serviceAccountDistinguishedName =
        confGetter.getGlobalConf(GlobalConfKeys.ldapServiceAccountDistinguishedName);
    String serviceAccountPassword =
        confGetter.getGlobalConf(GlobalConfKeys.ldapServiceAccountPassword);
    String ldapSearchAttribute = confGetter.getGlobalConf(GlobalConfKeys.ldapSearchAttribute);
    String ldapSearchFilter = confGetter.getGlobalConf(GlobalConfKeys.ldapSearchFilter);
    boolean enabledDetailedLogs = confGetter.getGlobalConf(GlobalConfKeys.enableDetailedLogs);
    String ldapGroupSearchFilter = confGetter.getGlobalConf(GlobalConfKeys.ldapGroupSearchFilter);
    SearchScope ldapGroupSearchScope =
        confGetter.getGlobalConf(GlobalConfKeys.ldapGroupSearchScope);
    String ldapGroupMemberOfAttribute =
        confGetter.getGlobalConf(GlobalConfKeys.ldapGroupMemberOfAttribute);
    boolean ldapGroupUseQuery = confGetter.getGlobalConf(GlobalConfKeys.ldapGroupUseQuery);
    boolean ldapGroupUseRoleMapping =
        confGetter.getGlobalConf(GlobalConfKeys.ldapGroupUseRoleMapping);
    String ldapGroupSearchBaseDn = confGetter.getGlobalConf(GlobalConfKeys.ldapGroupSearchBaseDn);
    Role ldapDefaultRole = confGetter.getGlobalConf(GlobalConfKeys.ldapDefaultRole);
    TlsProtocol ldapTlsProtocol = confGetter.getGlobalConf(GlobalConfKeys.ldapTlsProtocol);
    boolean useNewRbacAuthz = confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz);

    LdapConfiguration ldapConfiguration =
        new LdapConfiguration(
            ldapUrl,
            ldapPort,
            ldapBaseDN,
            ldapCustomerUUID,
            ldapDnPrefix,
            ldapUseSsl,
            ldapUseTls,
            useLdapSearchAndBind,
            serviceAccountDistinguishedName,
            serviceAccountPassword,
            ldapSearchAttribute,
            ldapSearchFilter,
            enabledDetailedLogs,
            ldapGroupSearchFilter,
            ldapGroupSearchScope,
            ldapGroupSearchBaseDn,
            ldapGroupMemberOfAttribute,
            ldapGroupUseQuery,
            ldapGroupUseRoleMapping,
            ldapDefaultRole,
            ldapTlsProtocol,
            useNewRbacAuthz);
    Users user = authViaLDAP(data.getEmail(), data.getPassword(), ldapConfiguration);

    if (user == null) {
      return user;
    }

    try {
      user.save();
    } catch (DuplicateKeyException e) {
      log.info("User already exists.");
    }
    return user;
  }

  private UUID getCustomerUUID(String ldapCustomerUUID, String ybaUsername) {
    // check if old user
    Users oldUser = Users.find.query().where().eq("email", ybaUsername).findOne();
    if (oldUser != null && oldUser.getCustomerUUID() != null) {
      return oldUser.getCustomerUUID();
    }

    Customer customer = null;
    if (!ldapCustomerUUID.equals("")) {
      try {
        UUID customerUUID = UUID.fromString(ldapCustomerUUID);
        customer = Customer.get(customerUUID);
      } catch (Exception e) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Customer UUID specified is invalid. " + e.getMessage());
      }
    }

    if (customer == null) {
      List<Customer> allCustomers = Customer.getAll();
      if (allCustomers.size() != 1) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Please specify ldap_customeruuid in Multi-Tenant Setup.");
      }
      customer = allCustomers.get(0);
    }

    return customer.getUuid();
  }

  private void deleteUserAndThrowException(String email) {
    Users.deleteUser(email);
    String errorMessage = "LDAP user " + email + " does not exist on the LDAP server";
    throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
  }

  public LdapNetworkConnection createNewLdapConnection(LdapConnectionConfig ldapConnectionConfig) {
    return new LdapNetworkConnection(ldapConnectionConfig);
  }

  private Set<String> getGroups(
      String ybaUsername,
      Entry userEntry,
      LdapNetworkConnection connection,
      LdapConfiguration ldapConfiguration) {
    Set<String> groups = new HashSet<String>();

    if (!ldapConfiguration.isLdapGroupUseQuery()) {
      Attribute memberOf = userEntry.get(ldapConfiguration.getLdapGroupMemberOfAttribute());
      if (memberOf != null) {
        for (Value group : memberOf) {
          groups.add(group.getString());
        }
      }
      return groups;
    }

    String searchFilter = "";
    try {
      searchFilter =
          ldapConfiguration.getLdapGroupSearchFilter().replace(USERNAME_KEYWORD, ybaUsername);
      EntryCursor cursor =
          connection.search(
              ldapConfiguration.getLdapGroupSearchBaseDn(),
              searchFilter,
              ldapConfiguration.getLdapGroupSearchScope(),
              "*");
      while (cursor.next()) {
        Entry entry = cursor.get();
        groups.add(entry.getDn().toString());
      }
    } catch (LdapException | CursorException e) {
      log.error(
          "Error querying groups with base dn: {} and search filter: {}",
          ldapConfiguration.getLdapBaseDN(),
          searchFilter);
    }

    return groups;
  }

  private Role getRoleMappedToLdapGroup(
      String group, UUID customerUuid, Set<UUID> groupMemberships) {
    Role role = null;
    GroupMappingInfo info =
        GroupMappingInfo.find
            .query()
            .where()
            .ieq("identifier", group)
            .eq("customer_uuid", customerUuid)
            .eq("type", "LDAP")
            .findOne();

    if (info != null) {
      groupMemberships.add(info.getGroupUUID());
      role =
          Role.valueOf(
              com.yugabyte.yw.models.rbac.Role.get(customerUuid, info.getRoleUUID()).getName());
    }
    return role;
  }

  private Role getRoleFromGroupMappings(
      Entry userEntry,
      String ybaUsername,
      LdapNetworkConnection connection,
      LdapConfiguration ldapConfiguration,
      UUID customerUuid,
      Set<UUID> groupMemberships) {
    Role role = null;
    Set<String> groups = getGroups(ybaUsername, userEntry, connection, ldapConfiguration);
    for (String group : groups) {
      Role groupRole = getRoleMappedToLdapGroup(group, customerUuid, groupMemberships);
      role = Role.union(role, groupRole);
    }
    return role;
  }

  private Triple<Entry, String, String> searchAndBind(
      String email,
      LdapConfiguration ldapConfiguration,
      LdapNetworkConnection connection,
      boolean enableDetailedLogs)
      throws Exception {
    Entry userEntry = null;
    String distinguishedName = "", role = "";

    try {
      connection.bind(
          ldapConfiguration.getServiceAccountDistinguishedName(),
          ldapConfiguration.getServiceAccountPassword());
    } catch (LdapAuthenticationException e) {
      String errorMessage = "Service Account bind failed. " + e.getMessage();
      log.error(errorMessage);
      throw new PlatformServiceException(UNAUTHORIZED, "Error binding to service account.");
    }
    try {
      String searchFilter = ldapConfiguration.getLdapSearchFilter();
      if (StringUtils.isEmpty(searchFilter)) {
        searchFilter = "(" + ldapConfiguration.getLdapSearchAttribute() + "=" + email + ")";
      }
      log.debug("Performing LDAP search with filter: {}", searchFilter);
      EntryCursor cursor =
          connection.search(
              ldapConfiguration.getLdapBaseDN(), searchFilter, SearchScope.SUBTREE, "*");
      log.info("Connection cursor: {}", cursor);
      while (cursor.next()) {
        userEntry = cursor.get();
        if (enableDetailedLogs) {
          log.info("LDAP server returned response: {}", userEntry.toString());
        }

        Attribute parseDn = userEntry.get("distinguishedName");
        log.info("parseDn: {}", parseDn);
        if (parseDn == null) {
          distinguishedName = userEntry.getDn().toString();
          log.info("parsedDn: {}", distinguishedName);
        } else {
          distinguishedName = parseDn.getString();
        }
        log.info("Distinguished name parsed: {}", distinguishedName);

        Attribute parseRole = userEntry.get("yugabytePlatformRole");
        if (parseRole != null) {
          role = parseRole.getString();
        }

        // Cursor.next returns true in some environments
        if (!StringUtils.isEmpty(distinguishedName)) {
          if (ldapConfiguration.isEnableDetailedLogs()) {
            log.debug("Successfully fetched user entry from LDAP Server {}", userEntry.toString());
          }
          break;
        }
      }

      try {
        cursor.close();
        connection.unBind();
      } catch (Exception e) {
        log.error("Failed closing connections", e);
      }
    } catch (Exception e) {
      log.error("LDAP query failed.", e);
      throw new PlatformServiceException(BAD_REQUEST, "LDAP search failed.");
    }
    return new ImmutableTriple<Entry, String, String>(userEntry, distinguishedName, role);
  }

  public LdapNetworkConnection createConnection(
      String ldapUrl, int ldapPort, boolean ldapUseSsl, boolean ldapUseTls, String tlsVersion)
      throws LdapException, NoSuchAlgorithmException, KeyStoreException {

    LdapConnectionConfig config = new LdapConnectionConfig();
    config.setLdapHost(ldapUrl);
    config.setLdapPort(ldapPort);
    if (ldapUseSsl || ldapUseTls) {
      config.setEnabledProtocols(new String[] {tlsVersion});

      boolean customCAUploaded = customCAStoreManager.areCustomCAsPresent();
      if (customCAStoreManager.isEnabled() && isCertVerificationEnforced()) {
        if (customCAUploaded) {
          log.debug("Using YBA's custom trust-store manager along-with Java defaults");
          KeyStore ybaJavaKeyStore = customCAStoreManager.getYbaAndJavaKeyStore();
          TrustManagerFactory trustFactory =
              TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
          trustFactory.init(ybaJavaKeyStore);
          TrustManager[] ybaJavaTrustManagers = trustFactory.getTrustManagers();
          config.setTrustManagers(ybaJavaTrustManagers);
        } else {
          log.debug("Using Java default trust managers");
        }
      } else {
        if (customCAUploaded) {
          log.warn(
              "Skipping to use YBA's trust-store as the feature is disabled. CA-store "
                  + "feature flag: {}, certification-verfication for LDAP: {}",
              customCAStoreManager.isEnabled(),
              isCertVerificationEnforced());
        }
        config.setTrustManagers(new NoVerificationTrustManager());
      }

      if (ldapUseSsl) {
        config.setUseSsl(true);
      } else {
        config.setUseTls(true);
      }
    }

    return createNewLdapConnection(config);
  }

  public Users authViaLDAP(String email, String password, LdapConfiguration ldapConfiguration)
      throws LdapException {
    Users users = new Users();
    Set<UUID> groupMemberships = new HashSet<>();
    LdapNetworkConnection connection = null;
    try {
      String distinguishedName =
          ldapConfiguration.getLdapDnPrefix() + email + "," + ldapConfiguration.getLdapBaseDN();
      connection =
          createConnection(
              ldapConfiguration.getLdapUrl(),
              ldapConfiguration.getLdapPort(),
              ldapConfiguration.isLdapUseSsl(),
              ldapConfiguration.isLdapUseTls(),
              ldapConfiguration.getLdapTlsProtocol().getVersionString());

      String role = "";
      Entry userEntry = null;
      if (ldapConfiguration.isUseLdapSearchAndBind()) {
        // search and bind
        if (ldapConfiguration.getServiceAccountDistinguishedName().isEmpty()
            || ldapConfiguration.getServiceAccountPassword().isEmpty()
            || (ldapConfiguration.getLdapSearchAttribute().isEmpty()
                && ldapConfiguration.getLdapSearchFilter().isEmpty())) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Service account and LDAP Search Attribute/Filter must be configured"
                  + " to use search and bind.");
        }
        // With search + bind, we first log in to the service account, fetch the user via the search
        // attribute/filter and retrieve the user's DN. Once we have the DN, we perform a bind with
        // the LDAP server for the user using this DN and the password entered on the login page.
        log.debug("Configuring authentication via LDAP Search and Bind");
        Triple<Entry, String, String> entryDnRole =
            searchAndBind(
                email, ldapConfiguration, connection, ldapConfiguration.isEnableDetailedLogs());
        userEntry = entryDnRole.getLeft();
        if (userEntry == null) {
          // search has returned an empty user => user doesn't exist on the LDAP server
          String errorMessage =
              "LDAP user "
                  + email
                  + " does not match any entries on the LDAP server based on the provided search"
                  + " attribute/filter.";
          throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
        }
        String fetchedDistinguishedName = entryDnRole.getMiddle();
        if (!fetchedDistinguishedName.isEmpty()) {
          distinguishedName = fetchedDistinguishedName;
          log.debug("Updated Dn: {}", distinguishedName);
        }
        role = entryDnRole.getRight();
      }

      // simple bind
      log.debug("Configuring authentication via LDAP Simple Bind");
      email = email.toLowerCase();
      try {
        if (ldapConfiguration.isEnableDetailedLogs()) {
          log.debug(
              "Binding to LDAP Server with distinguishedName: {} and password: {}",
              distinguishedName,
              "********");
        }
        connection.bind(distinguishedName, password);
      } catch (LdapNoSuchObjectException e) {
        log.error(e.getMessage());
        deleteUserAndThrowException(email);
      } catch (LdapAuthenticationException e) {
        log.error(e.getMessage());
        if (e.getMessage().contains(windowsAdUserDoesNotExistErrorCode)) {
          deleteUserAndThrowException(email);
        }
        String errorMessage = "Failed with " + e.getMessage();
        throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
      }

      // User has been authenticated.
      users.setCustomerUUID(getCustomerUUID(ldapConfiguration.getLdapCustomerUUID(), email));

      if (role.isEmpty() && !ldapConfiguration.isUseLdapSearchAndBind()) {
        if (!ldapConfiguration.getServiceAccountDistinguishedName().isEmpty()
            && !ldapConfiguration.getServiceAccountPassword().isEmpty()) {
          connection.unBind();
          try {
            if (ldapConfiguration.isEnableDetailedLogs()) {
              log.debug(
                  "Binding to LDAP Server with distinguishedName: {} and password: {}",
                  ldapConfiguration.getServiceAccountDistinguishedName(),
                  "********");
            }
            connection.bind(
                ldapConfiguration.getServiceAccountDistinguishedName(),
                ldapConfiguration.getServiceAccountPassword());
          } catch (LdapAuthenticationException e) {
            String errorMessage =
                "Service Account bind failed. "
                    + "Defaulting to current user connection with LDAP Server."
                    + e.getMessage();
            log.error(errorMessage);
            if (ldapConfiguration.isEnableDetailedLogs()) {
              log.debug(
                  "Binding to LDAP Server with distinguishedName: {} and password: {}",
                  distinguishedName,
                  "********");
            }
            connection.bind(distinguishedName, password);
          }
        }

        try {
          EntryCursor cursor =
              connection.search(distinguishedName, "(objectclass=*)", SearchScope.SUBTREE, "*");
          while (cursor.next()) {
            userEntry = cursor.get();
            if (ldapConfiguration.isEnableDetailedLogs()) {
              log.info("LDAP server returned response: {}", userEntry.toString());
            }
            Attribute parseRole = userEntry.get("yugabytePlatformRole");
            role = parseRole.getString();
          }
        } catch (Exception e) {
          log.debug(
              String.format("LDAP query for yugabytePlatformRole failed with %s", e.getMessage()));
        }
      }

      if (ldapConfiguration.isLdapGroupUseRoleMapping()) {
        log.debug("Mapping roles to LDAP groups...");
        if (ldapConfiguration.getServiceAccountDistinguishedName().isEmpty()
            || ldapConfiguration.getServiceAccountPassword().isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Service account must be configured to use group to role mapping.");
        }
        connection.unBind();
        try {
          if (ldapConfiguration.isEnableDetailedLogs()) {
            log.debug(
                "Binding to LDAP Server with distinguishedName: {} and password: {}",
                ldapConfiguration.getServiceAccountDistinguishedName(),
                "********");
          }
          connection.bind(
              ldapConfiguration.getServiceAccountDistinguishedName(),
              ldapConfiguration.getServiceAccountPassword());
        } catch (LdapAuthenticationException e) {
          String errorMessage = "Service Account bind failed. " + e.getMessage();
          log.error(errorMessage);
          throw new PlatformServiceException(
              UNAUTHORIZED,
              "Error binding to service account. Cannot get group memberships for username: "
                  + email);
        }

        Role roleFromGroupMappings =
            getRoleFromGroupMappings(
                userEntry,
                email,
                connection,
                ldapConfiguration,
                users.getCustomerUUID(),
                groupMemberships);

        if (roleFromGroupMappings == null) {
          log.warn("No role mappings from LDAP group membership of user: " + email);
        }

        if (role.isEmpty()) {
          if (roleFromGroupMappings != null) {
            role = roleFromGroupMappings.toString();
            log.info(
                "No role found from yugabytePlatformRole, "
                    + "using role {} found from LDAP group mappings for user: {}",
                role,
                email);
          } else {
            log.warn(
                "No role found from either yugabytePlatformRole or "
                    + "LDAP group mapping for LDAP user: {}",
                email);
          }
        } else {
          try {
            Role roleEnum = Role.valueOf(role);
            role = Role.union(roleEnum, roleFromGroupMappings).toString();
            log.info(
                "Roles from yugabytePlatformRole and LDAP group memberships for LDAP user: "
                    + "{} combined to assign {}",
                email,
                role);
          } catch (IllegalArgumentException e) {
            if (roleFromGroupMappings != null) {
              log.error(
                  "Invalid role: {} obtained from yugabytePlatformRole,"
                      + " attempting to use role mapped to LDAP groups: {}",
                  role,
                  roleFromGroupMappings);
              role = roleFromGroupMappings.toString();
            } else {
              log.error(
                  "Invalid role: \"{}\" obtained from yugabytePlatformRole, "
                      + "role mapped to LDAP groups is also null.",
                  role);
            }
          }
        }
      }

      DB.beginTransaction();

      Users.Role roleToAssign;
      users.setLdapSpecifiedRole(true);
      switch (role) {
        case "Admin":
          roleToAssign = Users.Role.Admin;
          break;
        case "SuperAdmin":
          roleToAssign = Users.Role.SuperAdmin;
          break;
        case "BackupAdmin":
          roleToAssign = Users.Role.BackupAdmin;
          break;
        case "ReadOnly":
          roleToAssign = Users.Role.ReadOnly;
          break;
        default:
          roleToAssign = ldapConfiguration.getLdapDefaultRole();
          users.setLdapSpecifiedRole(false);
      }

      Users oldUser = Users.find.query().where().eq("email", email).findOne();

      if (oldUser != null) {
        /* If we find a valid role from LDAP then assign that to this user and disallow further
         * changes by YBA SuperAdmin in User Management.
         * Otherwise,
         *  if the last time the role was set via LDAP, we take away that role (meaning assign
         *    default), and allow YBA SuperAdmin to change this role later in User Management.
         * Otherwise, this is a user for which the admin has or will set a role, NOOP
         */
        if (users.isLdapSpecifiedRole()) {
          if (ldapConfiguration.isEnableDetailedLogs()) {
            log.debug(
                "Assigning LDAP-specified role {} to existing user {}",
                roleToAssign,
                oldUser.getEmail());
          }
          oldUser.setRole(roleToAssign);
          oldUser.setLdapSpecifiedRole(true);
        } else if (oldUser.isLdapSpecifiedRole()) {
          log.warn("No valid role could be ascertained, defaulting to {}.", roleToAssign);
          oldUser.setRole(roleToAssign);
          oldUser.setLdapSpecifiedRole(false);
        }
        users = oldUser;
      } else {
        if (!users.isLdapSpecifiedRole()) {
          log.warn("No valid role could be ascertained, defaulting to {}.", roleToAssign);
        }

        users.setEmail(email.toLowerCase());
        byte[] passwordLdap = new byte[16];
        new Random().nextBytes(passwordLdap);
        String generatedPassword = new String(passwordLdap, Charset.forName("UTF-8"));
        users.setPassword(generatedPassword); // Password is not used.
        users.setUserType(Users.UserType.ldap);
        users.setCreationDate(new Date());
        users.setPrimary(false);
        users.setRole(roleToAssign);
      }
      if (ldapConfiguration.isEnableDetailedLogs()) {
        log.debug("Saving new user: {}", users);
      }
      users.setGroupMemberships(groupMemberships);
      users.save();

      if (ldapConfiguration.isUseNewRbacAuthz()) {
        log.debug("Using new RBAC authorization...");
        List<RoleBinding> currentRoleBindings = RoleBinding.getAll(users.getUuid());
        currentRoleBindings.stream().forEach(rB -> rB.delete());
        com.yugabyte.yw.models.rbac.Role newRbacRole =
            com.yugabyte.yw.models.rbac.Role.get(users.getCustomerUUID(), roleToAssign.name());
        if (newRbacRole != null) {
          log.debug("Found role with name: {}", roleToAssign.name());
          ResourceGroup rG =
              ResourceGroup.getSystemDefaultResourceGroup(users.getCustomerUUID(), users);
          RoleBinding createdRoleBinding =
              RoleBinding.create(users, RoleBindingType.System, newRbacRole, rG);
          if (ldapConfiguration.isEnableDetailedLogs()) {
            log.debug("Created role binding: {}", createdRoleBinding);
          }
        } else {
          throw new RuntimeException(String.format("No role with the name: %s found", role));
        }
      }
      DB.commitTransaction();

    } catch (LdapConnectionTimeOutException e) {
      String errorMessage =
          "Cannot reach the LDAP server. Verify the server configuration and ensure it's running on"
              + " the specified setting.";
      log.error(errorMessage, e);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    } catch (LdapException e) {
      log.error(String.format("LDAP error while attempting to auth email %s", email), e);
      throw e;
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error(String.format("Failed to authenticate with LDAP for email %s", email), e);
      String errorMessage = "Invalid LDAP credentials. " + e.getMessage();
      throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
    } finally {
      if (connection != null && connection.isConnected()) {
        connection.unBind();
        connection.close();
      }
      DB.endTransaction();
    }
    return users;
  }

  public boolean isCertVerificationEnforced() {
    return confGetter.getGlobalConf(GlobalConfKeys.ldapsEnforceCertVerification);
  }

  private String getNameFromDN(String dn) {
    String[] attributeValuePairs = dn.split(",");
    String[] attributeValue = attributeValuePairs[0].split("=");
    return attributeValue[1].trim();
  }
}

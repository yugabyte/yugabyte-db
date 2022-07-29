package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.SessionController;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DuplicateKeyException;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.apache.directory.api.ldap.model.cursor.EntryCursor;
import org.apache.directory.api.ldap.model.entry.Attribute;
import org.apache.directory.api.ldap.model.entry.Entry;
import org.apache.directory.api.ldap.model.exception.LdapAuthenticationException;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.apache.directory.api.ldap.model.exception.LdapNoSuchObjectException;
import org.apache.directory.api.ldap.model.message.SearchScope;
import org.apache.directory.ldap.client.api.LdapConnectionConfig;
import org.apache.directory.ldap.client.api.LdapNetworkConnection;
import org.apache.directory.ldap.client.api.NoVerificationTrustManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.charset.Charset;
import java.util.Date;
import java.util.List;
import java.util.Random;
import java.util.UUID;

import static play.mvc.Http.Status.*;

@Slf4j
public class LdapUtil {

  public static final Logger LOG = LoggerFactory.getLogger(SessionController.class);
  public static final String windowsAdUserDoesNotExistErrorCode = "data 2030";

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
    String serviceAccountUserName;
    String serviceAccountPassword;
    String ldapSearchAttribute;
  }

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  public Users loginWithLdap(CustomerLoginFormData data) throws LdapException {
    String ldapUrl =
        runtimeConfigFactory.globalRuntimeConf().getString("yb.security.ldap.ldap_url");
    String getLdapPort =
        runtimeConfigFactory.globalRuntimeConf().getString("yb.security.ldap.ldap_port");
    Integer ldapPort = Integer.parseInt(getLdapPort);
    String ldapBaseDN =
        runtimeConfigFactory.globalRuntimeConf().getString("yb.security.ldap.ldap_basedn");
    String ldapCustomerUUID =
        runtimeConfigFactory.globalRuntimeConf().getString("yb.security.ldap.ldap_customeruuid");
    String ldapDnPrefix =
        runtimeConfigFactory.globalRuntimeConf().getString("yb.security.ldap.ldap_dn_prefix");
    boolean ldapUseSsl =
        runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ldap.enable_ldaps");
    boolean ldapUseTls =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getBoolean("yb.security.ldap.enable_ldap_start_tls");
    boolean useLdapSearchAndBind =
        runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ldap.use_search_and_bind");
    String serviceAccountUserName =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.security.ldap.ldap_service_account_username");
    String serviceAccountPassword =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.security.ldap.ldap_service_account_password");
    String ldapSearchAttribute =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.security.ldap.ldap_search_attribute");

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
            serviceAccountUserName,
            serviceAccountPassword,
            ldapSearchAttribute);
    Users user = authViaLDAP(data.getEmail(), data.getPassword(), ldapConfiguration);

    if (user == null) {
      return user;
    }

    if (user.customerUUID == null) {
      Customer cust = null;
      if (!ldapCustomerUUID.equals("")) {
        try {
          UUID custUUID = UUID.fromString(ldapCustomerUUID);
          cust = Customer.get(custUUID);
        } catch (Exception e) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Customer UUID Specified is invalid. " + e.getMessage());
        }
      }
      if (cust == null) {
        List<Customer> allCustomers = Customer.getAll();
        if (allCustomers.size() != 1) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Please specify ldap_customeruuid in Multi-Tenant Setup.");
        }
        cust = allCustomers.get(0);
      }
      user.setCustomerUuid(cust.uuid);
    }
    try {
      user.save();
    } catch (DuplicateKeyException e) {
      log.info("User already exists.");
    }
    return user;
  }

  private void deleteUserAndThrowException(String email) {
    Users.deleteUser(email);
    String errorMessage = "LDAP user " + email + " does not exist on the LDAP server";
    throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
  }

  public LdapNetworkConnection createNewLdapConnection(LdapConnectionConfig ldapConnectionConfig) {
    return new LdapNetworkConnection(ldapConnectionConfig);
  }

  private Pair<String, String> searchAndBind(
      String email, LdapConfiguration ldapConfiguration, LdapNetworkConnection connection)
      throws Exception {
    String distinguishedName = "", role = "";
    String serviceAccountDistinguishedName =
        ldapConfiguration.getLdapDnPrefix()
            + ldapConfiguration.getServiceAccountUserName()
            + ","
            + ldapConfiguration.getLdapBaseDN();
    try {
      connection.bind(
          serviceAccountDistinguishedName, ldapConfiguration.getServiceAccountPassword());
    } catch (LdapAuthenticationException e) {
      String errorMessage = "Service Account bind failed. " + e.getMessage();
      log.error(errorMessage);
      throw new PlatformServiceException(UNAUTHORIZED, "Error binding to service account.");
    }
    try {
      EntryCursor cursor =
          connection.search(
              ldapConfiguration.getLdapBaseDN(),
              "(" + ldapConfiguration.getLdapSearchAttribute() + "=" + email + ")",
              SearchScope.SUBTREE,
              "*");
      while (cursor.next()) {
        Entry entry = cursor.get();
        Attribute parseDn = entry.get("distinguishedName");
        distinguishedName = parseDn.getString();
        Attribute parseRole = entry.get("yugabytePlatformRole");
        if (parseRole != null) {
          role = parseRole.getString();
        }
      }
      cursor.close();
      connection.unBind();
    } catch (Exception e) {
      log.error(String.format("LDAP query failed with %s.", e.getMessage()));
      throw new PlatformServiceException(BAD_REQUEST, "LDAP search failed.");
    }
    return new ImmutablePair<>(distinguishedName, role);
  }

  public Users authViaLDAP(String email, String password, LdapConfiguration ldapConfiguration)
      throws LdapException {
    Users users = new Users();
    LdapNetworkConnection connection = null;
    try {
      LdapConnectionConfig config = new LdapConnectionConfig();
      config.setLdapHost(ldapConfiguration.getLdapUrl());
      config.setLdapPort(ldapConfiguration.getLdapPort());
      if (ldapConfiguration.isLdapUseSsl() || ldapConfiguration.isLdapUseTls()) {
        config.setTrustManagers(new NoVerificationTrustManager());
        if (ldapConfiguration.isLdapUseSsl()) {
          config.setUseSsl(true);
        } else {
          config.setUseTls(true);
        }
      }

      String distinguishedName =
          ldapConfiguration.getLdapDnPrefix() + email + "," + ldapConfiguration.getLdapBaseDN();
      connection = createNewLdapConnection(config);

      String role = "";
      if (ldapConfiguration.isUseLdapSearchAndBind()) {
        if (ldapConfiguration.getServiceAccountUserName().isEmpty()
            || ldapConfiguration.getServiceAccountPassword().isEmpty()
            || ldapConfiguration.getLdapSearchAttribute().isEmpty()) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Service account and LDAP Search Attribute must be configured"
                  + " to use search and bind.");
        }
        Pair<String, String> dnAndRole = searchAndBind(email, ldapConfiguration, connection);
        distinguishedName = dnAndRole.getKey();
        role = dnAndRole.getValue();
      }

      email = email.toLowerCase();
      try {
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

      if (role.isEmpty() && !ldapConfiguration.isUseLdapSearchAndBind()) {
        if (!ldapConfiguration.getServiceAccountUserName().isEmpty()
            && !ldapConfiguration.getServiceAccountPassword().isEmpty()) {
          connection.unBind();
          String serviceAccountDistinguishedName =
              ldapConfiguration.getLdapDnPrefix()
                  + ldapConfiguration.getServiceAccountUserName()
                  + ","
                  + ldapConfiguration.getLdapBaseDN();
          try {
            connection.bind(
                serviceAccountDistinguishedName, ldapConfiguration.getServiceAccountPassword());
          } catch (LdapAuthenticationException e) {
            String errorMessage =
                "Service Account bind failed. "
                    + "Defaulting to current user connection with LDAP Server."
                    + e.getMessage();
            log.error(errorMessage);
            connection.bind(distinguishedName, password);
          }
        }

        try {
          EntryCursor cursor =
              connection.search(distinguishedName, "(objectclass=*)", SearchScope.SUBTREE, "*");
          while (cursor.next()) {
            Entry entry = cursor.get();
            Attribute parseRole = entry.get("yugabytePlatformRole");
            role = parseRole.getString();
          }
        } catch (Exception e) {
          log.debug(
              String.format(
                  "LDAP query failed with {} Defaulting to ReadOnly role. %s", e.getMessage()));
        }
      }

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
          roleToAssign = Users.Role.ReadOnly;
          users.setLdapSpecifiedRole(false);
      }
      Users oldUser = Users.find.query().where().eq("email", email).findOne();
      if (oldUser != null
          && (oldUser.getRole() == roleToAssign || !oldUser.getLdapSpecifiedRole())) {
        return oldUser;
      } else if (oldUser != null && (oldUser.getRole() != roleToAssign)) {
        oldUser.setRole(roleToAssign);
        return oldUser;
      } else {
        users.email = email.toLowerCase();
        byte[] passwordLdap = new byte[16];
        new Random().nextBytes(passwordLdap);
        String generatedPassword = new String(passwordLdap, Charset.forName("UTF-8"));
        users.setPassword(generatedPassword); // Password is not used.
        users.setUserType(Users.UserType.ldap);
        users.creationDate = new Date();
        users.setIsPrimary(false);
        users.setRole(roleToAssign);
      }
    } catch (LdapException e) {
      LOG.error("LDAP error while attempting to auth email {}", email);
      LOG.debug(e.getMessage());
      String errorMessage = "LDAP parameters are not configured correctly. " + e.getMessage();
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    } catch (Exception e) {
      LOG.error("Failed to authenticate with LDAP for email {}", email);
      LOG.debug(e.getMessage());
      String errorMessage = "Invalid LDAP credentials. " + e.getMessage();
      throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
    } finally {
      if (connection != null) {
        connection.unBind();
        connection.close();
      }
    }
    return users;
  }
}

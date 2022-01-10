package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.SessionController;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DuplicateKeyException;
import lombok.extern.slf4j.Slf4j;
import org.apache.directory.api.ldap.model.cursor.EntryCursor;
import org.apache.directory.api.ldap.model.entry.Attribute;
import org.apache.directory.api.ldap.model.entry.Entry;
import org.apache.directory.api.ldap.model.exception.LdapAuthenticationException;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.apache.directory.api.ldap.model.exception.LdapNoSuchObjectException;
import org.apache.directory.api.ldap.model.message.SearchScope;
import org.apache.directory.ldap.client.api.LdapNetworkConnection;
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

    Users user =
        authViaLDAP(
            data.getEmail().toLowerCase(), data.getPassword(), ldapUrl, ldapPort, ldapBaseDN);
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

  private Users authViaLDAP(
      String email, String password, String ldapUrl, Integer ldapPort, String ldapBaseDN)
      throws LdapException {
    Users users = new Users();
    LdapNetworkConnection connection = null;
    try {
      connection = new LdapNetworkConnection(ldapUrl, ldapPort);
      String distinguishedName = ldapBaseDN + "CN=" + email;
      try {
        connection.bind(distinguishedName, password);
      } catch (LdapNoSuchObjectException e) {
        Users.deleteUser(email);
        String errorMessage = "LDAP user " + email + " does not exist on the LDAP server";
        throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
      } catch (LdapAuthenticationException e) {
        String errorMessage = "Failed with " + e.getMessage();
        throw new PlatformServiceException(UNAUTHORIZED, errorMessage);
      }
      EntryCursor cursor =
          connection.search(distinguishedName, "(objectclass=*)", SearchScope.SUBTREE, "*");
      while (cursor.next()) {
        Entry entry = cursor.get();
        Attribute parseRole = entry.get("YugabytePlatformRole");
        String role = parseRole.getString();
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

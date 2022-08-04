// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.encryption.HashBuilder;
import com.yugabyte.yw.common.encryption.bc.BcOpenBsdHasher;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;

import io.ebean.DuplicateKeyException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.math.BigInteger;
import java.security.SecureRandom;
import java.util.Calendar;
import java.util.Date;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.Transient;

import lombok.extern.slf4j.Slf4j;

import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.mvc.Http.Status;

@Slf4j
@Entity
@ApiModel(description = "A user associated with a customer")
public class Users extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Users.class);

  private static final HashBuilder hasher = new BcOpenBsdHasher();

  /** These are the available user roles */
  public enum Role {
    @EnumValue("Admin")
    Admin,

    @EnumValue("ReadOnly")
    ReadOnly,

    @EnumValue("SuperAdmin")
    SuperAdmin,

    @EnumValue("BackupAdmin")
    BackupAdmin;

    public String getFeaturesFile() {
      switch (this) {
        case Admin:
          return null;
        case ReadOnly:
          return "readOnlyFeatureConfig.json";
        case SuperAdmin:
          return null;
        case BackupAdmin:
          return "backupAdminFeatureConfig.json";
        default:
          return null;
      }
    }
  }

  public enum UserType {
    @EnumValue("local")
    local,

    @EnumValue("ldap")
    ldap;
  }

  // A globally unique UUID for the Users.
  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "User UUID", accessMode = READ_ONLY)
  public UUID uuid = UUID.randomUUID();

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  public UUID customerUUID;

  public void setCustomerUuid(UUID id) {
    this.customerUUID = id;
  }

  @Column(length = 256, unique = true, nullable = false)
  @Constraints.Required
  @Constraints.Email
  @ApiModelProperty(
      value = "User email address",
      example = "username1@example.com",
      required = true)
  public String email;

  public String getEmail() {
    return this.email;
  }

  @JsonIgnore
  @Column(length = 256, nullable = false)
  @ApiModelProperty(
      value = "User password hash",
      example = "$2y$10$ABccHWa1DO2VhcF1Ea2L7eOBZRhktsJWbFaB/aEjLfpaplDBIJ8K6")
  public String passwordHash;

  public void setPassword(String password) {
    this.passwordHash = Users.hasher.hash(password);
  }

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssXXX")
  @ApiModelProperty(
      value = "User creation date",
      example = "2021-06-17T15:00:05-04:00",
      accessMode = READ_ONLY)
  public Date creationDate;

  private String authToken;

  @Column(nullable = true)
  @ApiModelProperty(
      value = "API token creation date",
      example = "1624255408795",
      accessMode = READ_ONLY)
  private Date authTokenIssueDate;

  @JsonIgnore
  @Column(nullable = true)
  @ApiModelProperty(value = "User API token", accessMode = READ_ONLY)
  private String apiToken;

  @Column(nullable = true)
  @ApiModelProperty(value = "User timezone")
  private String timezone;

  // The role of the user.
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  @ApiModelProperty(value = "User role")
  private Role role;

  public Role getRole() {
    return this.role;
  }

  public void setRole(Role role) {
    this.role = role;
  }

  @Column(nullable = false)
  @ApiModelProperty(value = "True if the user is the primary user")
  private boolean isPrimary;

  public boolean getIsPrimary() {
    return this.isPrimary;
  }

  public void setIsPrimary(boolean isPrimary) {
    this.isPrimary = isPrimary;
  }

  @Column(nullable = false)
  @ApiModelProperty(value = "User Type")
  public UserType userType;

  public void setUserType(UserType userType) {
    this.userType = userType;
  }

  public UserType getUserType() {
    return this.userType;
  }

  @Column(nullable = false)
  @ApiModelProperty(value = "LDAP Specified Role")
  public boolean ldapSpecifiedRole;

  public void setLdapSpecifiedRole(boolean ldapSpecifiedRole) {
    this.ldapSpecifiedRole = ldapSpecifiedRole;
  }

  public boolean getLdapSpecifiedRole() {
    return this.ldapSpecifiedRole;
  }

  public Date getAuthTokenIssueDate() {
    return this.authTokenIssueDate;
  }

  public String getTimezone() {
    return this.timezone;
  }

  public void setTimezone(String timezone) {
    this.timezone = timezone;
  }

  public static final Finder<UUID, Users> find = new Finder<UUID, Users>(Users.class) {};

  @Deprecated
  public static Users get(UUID userUUID) {
    return find.query().where().eq("uuid", userUUID).findOne();
  }

  public static Users getOrBadRequest(UUID userUUID) {
    Users user = get(userUUID);
    if (user == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid User UUID:" + userUUID);
    }
    return user;
  }

  public static List<Users> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public Users() {
    this.creationDate = new Date();
  }

  /**
   * Create new Users, we encrypt the password before we store it in the DB
   *
   * @param email
   * @param password
   * @return Newly Created Users
   */
  public static Users create(
      String email, String password, Role role, UUID customerUUID, boolean isPrimary) {
    try {
      return createInternal(email, password, role, customerUUID, isPrimary);
    } catch (DuplicateKeyException pe) {
      throw new PlatformServiceException(Status.CONFLICT, "User already exists");
    }
  }

  /**
   * Create first Users associated to a customer, we encrypt the password before we store it in the
   * DB
   *
   * @return Newly Created Primary User
   */
  public static Users createPrimary(String email, String password, Role role, UUID customerUUID) {
    try {
      return createInternal(email, password, role, customerUUID, true);
    } catch (DuplicateKeyException pe) {
      throw new PlatformServiceException(Status.CONFLICT, "Customer already registered.");
    }
  }

  static Users createInternal(
      String email, String password, Role role, UUID customerUUID, boolean isPrimary) {
    Users users = new Users();
    users.email = email.toLowerCase();
    users.setPassword(password);
    users.setCustomerUuid(customerUUID);
    users.creationDate = new Date();
    users.role = role;
    users.isPrimary = isPrimary;
    users.setUserType(UserType.local);
    users.setLdapSpecifiedRole(false);
    users.save();
    return users;
  }

  /**
   * Delete Users identified via email
   *
   * @param email
   * @return void
   */
  public static void deleteUser(String email) {
    Users userToDelete = Users.find.query().where().eq("email", email).findOne();
    if (userToDelete != null && userToDelete.userType.equals(UserType.ldap)) {
      log.info("Deleting user id {} with email address {}", userToDelete.uuid, userToDelete.email);
      userToDelete.delete();
    }
    return;
  }

  /**
   * Validate if the email and password combination is valid, we use this to authenticate the Users.
   *
   * @param email
   * @param password
   * @return Authenticated Users Info
   */
  public static Users authWithPassword(String email, String password) {
    Users users = Users.find.query().where().eq("email", email).findOne();

    if (users != null && Users.hasher.isValid(password, users.passwordHash)) {
      return users;
    } else {
      return null;
    }
  }

  /**
   * Validate if the email and password combination is valid, we use this to authenticate the Users.
   *
   * @param email
   * @return Authenticated Users Info
   */
  public static Users getByEmail(String email) {
    if (email == null) {
      return null;
    }

    return Users.find.query().where().eq("email", email).findOne();
  }

  /**
   * Create a random auth token for the Users and store it in the DB.
   *
   * @return authToken
   */
  public String createAuthToken() {
    Date tokenExpiryDate = new DateTime().minusDays(1).toDate();
    if (authTokenIssueDate == null || authTokenIssueDate.before(tokenExpiryDate)) {
      SecureRandom randomGenerator = new SecureRandom();
      // Keeping the length as 128 bits.
      byte[] randomBytes = new byte[16];
      randomGenerator.nextBytes(randomBytes);
      // Converting to hexadecimal encoding
      authToken = new BigInteger(1, randomBytes).toString(16);
      authTokenIssueDate = new Date();
      save();
    }
    return authToken;
  }

  public void setAuthToken(String authToken) {
    this.authToken = authToken;
    save();
  }

  /**
   * Create a random auth token without expiry date for Users and store it in the DB.
   *
   * @return apiToken
   */
  public String upsertApiToken() {
    apiToken = UUID.randomUUID().toString();
    save();
    return apiToken;
  }

  /**
   * Get current apiToken.
   *
   * @return apiToken
   */
  public String getApiToken() {
    if (apiToken == null) {
      return null;
    }
    return apiToken;
  }

  /**
   * Authenticate with Token, would check if the authToken is valid.
   *
   * @param authToken
   * @return Authenticated Users Info
   */
  public static Users authWithToken(String authToken, Duration authTokenExpiry) {
    if (authToken == null) {
      return null;
    }

    try {
      Users userWithToken = find.query().where().eq("authToken", authToken).findOne();
      if (userWithToken != null) {
        long tokenExpiryDuration = authTokenExpiry.toMinutes();
        int tokenExpiryInMinutes = (int) tokenExpiryDuration;
        Calendar calTokenExpiryDate = Calendar.getInstance();
        calTokenExpiryDate.setTime(userWithToken.authTokenIssueDate);
        calTokenExpiryDate.add(Calendar.MINUTE, tokenExpiryInMinutes);
        Calendar calCurrentDate = Calendar.getInstance();
        long tokenDiffMinutes =
            Duration.between(calCurrentDate.toInstant(), calTokenExpiryDate.toInstant())
                .toMinutes();

        // Call deleteAuthToken to delete authToken and its issueDate for that specific user
        if (tokenDiffMinutes <= 0) {
          userWithToken.deleteAuthToken();
        }
      }
      return userWithToken;
    } catch (Exception e) {
      return null;
    }
  }

  /**
   * Authenticate with API Token, would check if apiToken is valid.
   *
   * @param apiToken
   * @return Authenticated Users Info
   */
  public static Users authWithApiToken(String apiToken) {
    if (apiToken == null) {
      return null;
    }

    try {
      return find.query().where().eq("apiToken", apiToken).findOne();
    } catch (Exception e) {
      return null;
    }
  }

  /** Delete authToken for the Users. */
  public void deleteAuthToken() {
    authToken = null;
    authTokenIssueDate = null;
    save();
  }

  public static String getAllEmailsForCustomer(UUID customerUUID) {
    List<Users> users = Users.getAll(customerUUID);
    return users.stream().map(user -> user.email).collect(Collectors.joining(","));
  }

  public static List<Users> getAllReadOnly() {
    return find.query().where().eq("role", Role.ReadOnly).findList();
  }
}

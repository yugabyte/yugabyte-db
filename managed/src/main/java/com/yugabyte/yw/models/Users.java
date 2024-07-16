// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.Util.NULL_UUID;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.encryption.HashBuilder;
import com.yugabyte.yw.common.encryption.bc.BcOpenBsdHasher;
import io.ebean.DuplicateKeyException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Encrypted;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.math.BigInteger;
import java.security.SecureRandom;
import java.time.Duration;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.mvc.Http.Status;

@Slf4j
@Entity
@ApiModel(description = "A user associated with a customer")
@Getter
@Setter
public class Users extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Users.class);

  private static final HashBuilder hasher = new BcOpenBsdHasher();

  private static final KeyLock<UUID> usersLock = new KeyLock<>();

  /** These are the available user roles */
  public enum Role {
    @EnumValue("ConnectOnly")
    ConnectOnly,

    @EnumValue("ReadOnly")
    ReadOnly,

    @EnumValue("BackupAdmin")
    BackupAdmin,

    @EnumValue("Admin")
    Admin,

    @EnumValue("SuperAdmin")
    SuperAdmin;

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
        case ConnectOnly:
          return "connectOnlyFeatureConfig.json";
        default:
          return null;
      }
    }

    public static Role union(Role r1, Role r2) {
      if (r1 == null) {
        return r2;
      }

      if (r2 == null) {
        return r1;
      }

      if (r1.compareTo(r2) < 0) {
        return r2;
      }

      return r1;
    }
  }

  public enum UserType {
    @EnumValue("local")
    local,

    @EnumValue("ldap")
    ldap,

    @EnumValue("oidc")
    oidc;
  }

  // A globally unique UUID for the Users.
  @Id
  @ApiModelProperty(value = "User UUID", accessMode = READ_ONLY)
  private UUID uuid = UUID.randomUUID();

  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @Constraints.Required
  @Constraints.Email
  @ApiModelProperty(
      value = "User email address",
      example = "username1@example.com",
      required = true)
  private String email;

  @JsonIgnore
  @ApiModelProperty(
      value = "User password hash",
      example = "$2y$10$ABccHWa1DO2VhcF1Ea2L7eOBZRhktsJWbFaB/aEjLfpaplDBIJ8K6")
  private String passwordHash;

  public void setPassword(String password) {
    this.setPasswordHash(Users.hasher.hash(password));
  }

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "User creation date",
      example = "2022-12-12T13:07:18Z",
      accessMode = READ_ONLY)
  private Date creationDate;

  @Encrypted @JsonIgnore private String authToken;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "UI session token creation date",
      example = "2021-06-17T15:00:05Z",
      accessMode = READ_ONLY)
  private Date authTokenIssueDate;

  @ApiModelProperty(value = "Hash of API Token")
  @JsonIgnore
  private String apiToken;

  @JsonIgnore private Long apiTokenVersion = 0L;

  @ApiModelProperty(value = "User timezone")
  private String timezone;

  // The role of the user.
  @ApiModelProperty(value = "User role")
  private Role role;

  @ApiModelProperty(value = "True if the user is the primary user")
  @JsonProperty("isPrimary")
  private boolean isPrimary;

  @ApiModelProperty(value = "User Type")
  private UserType userType;

  @ApiModelProperty(value = "LDAP Specified Role")
  private boolean ldapSpecifiedRole;

  @Encrypted
  @Setter
  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  private String oidcJwtAuthToken;

  public String getOidcJwtAuthToken() {
    return null;
  }

  @JsonIgnore
  public String getUnmakedOidcJwtAuthToken() {
    return oidcJwtAuthToken;
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

  public static Users getOrBadRequest(UUID customerUUID, UUID userUUID) {
    Users user = find.query().where().idEq(userUUID).eq("customer_uuid", customerUUID).findOne();
    if (user == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Invalid User UUID: '%s' for customer: '%s'.", userUUID, customerUUID));
    }
    return user;
  }

  public static List<Users> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static List<Users> getAll() {
    return find.query().where().findList();
  }

  public Users() {
    this.setCreationDate(new Date());
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
      return createInternal(email, password, role, customerUUID, isPrimary, UserType.local);
    } catch (DuplicateKeyException pe) {
      throw new PlatformServiceException(Status.CONFLICT, "User already exists");
    }
  }

  /**
   * Create new Users, we encrypt the password before we store it in the DB
   *
   * @return Newly Created Users
   */
  public static Users create(
      String email,
      String password,
      Role role,
      UUID customerUUID,
      boolean isPrimary,
      UserType userType) {
    try {
      return createInternal(email, password, role, customerUUID, isPrimary, userType);
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
      return createInternal(email, password, role, customerUUID, true, UserType.local);
    } catch (DuplicateKeyException pe) {
      throw new PlatformServiceException(Status.CONFLICT, "Customer already registered.");
    }
  }

  static Users createInternal(
      String email,
      String password,
      Role role,
      UUID customerUUID,
      boolean isPrimary,
      UserType userType) {
    Users users = new Users();
    users.setEmail(email.toLowerCase());
    users.setPassword(password);
    users.setCustomerUUID(customerUUID);
    users.setCreationDate(new Date());
    users.setRole(role);
    users.setPrimary(isPrimary);
    users.setUserType(userType);
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
    if (userToDelete != null && userToDelete.getUserType().equals(UserType.ldap)) {
      log.info(
          "Deleting user id {} with email address {}",
          userToDelete.getUuid(),
          userToDelete.getEmail());
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

    if (users != null && Users.hasher.isValid(password, users.getPasswordHash())) {
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

  public void updateAuthToken(String authToken) {
    this.authToken = authToken;
    save();
  }

  /**
   * Create a random auth token without expiry date for Users and store it in the DB.
   *
   * @return apiToken
   */
  public String upsertApiToken() {
    return upsertApiToken(apiTokenVersion);
  }

  public String upsertApiToken(Long version) {
    UUID uuidToLock = uuid != null ? uuid : NULL_UUID;
    usersLock.acquireLock(uuidToLock);
    try {
      if (version != null
          && version != -1
          && apiTokenVersion != null
          && !version.equals(apiTokenVersion)) {
        throw new PlatformServiceException(BAD_REQUEST, "API token version has changed");
      }
      String apiTokenUnhashed = UUID.randomUUID().toString();
      apiToken = Users.hasher.hash(apiTokenUnhashed);

      apiTokenVersion = apiTokenVersion == null ? 1L : apiTokenVersion + 1;
      save();
      return apiTokenUnhashed;
    } finally {
      usersLock.releaseLock(uuidToLock);
    }
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
      List<Users> usersList = find.query().where().isNotNull("apiToken").findList();
      for (Users user : usersList) {
        if (Users.hasher.isValid(apiToken, user.getApiToken())) {
          return user;
        }
      }
      return null;
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
    return users.stream().map(user -> user.getEmail()).collect(Collectors.joining(","));
  }

  public static List<Users> getAllReadOnly() {
    return find.query().where().eq("role", Role.ReadOnly).findList();
  }

  @RequiredArgsConstructor
  public static class UserOIDCAuthToken {

    @ApiModelProperty(value = "User OIDC Auth token")
    public final String oidcAuthToken;
  }
}

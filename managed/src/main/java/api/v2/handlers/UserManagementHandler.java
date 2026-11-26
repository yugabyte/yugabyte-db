// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;

import api.v2.mappers.UserMapper;
import api.v2.models.UserCreateSpec;
import api.v2.models.UserCreation;
import api.v2.models.UserSpec;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.models.Users;
import io.ebean.annotation.Transactional;
import java.util.UUID;
import org.apache.commons.validator.routines.EmailValidator;

@Singleton
public class UserManagementHandler {

  private final RuntimeConfGetter confGetter;
  private final PasswordPolicyService passwordPolicyService;
  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public UserManagementHandler(
      RuntimeConfGetter confGetter,
      PasswordPolicyService passwordPolicyService,
      RoleBindingUtil roleBindingUtil) {
    this.confGetter = confGetter;
    this.passwordPolicyService = passwordPolicyService;
    this.roleBindingUtil = roleBindingUtil;
  }

  public UserCreation createUser(UUID cUUID, UserCreateSpec createSpec) {
    UserSpec spec = createSpec.getSpec();
    if (!EmailValidator.getInstance(true /* allowLocal */).isValid(spec.getEmail())) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid email: " + spec.getEmail());
    }
    if (spec.getRole() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "spec.role is required");
    }
    if (spec.getRole() == UserSpec.RoleEnum.SUPERADMIN) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot create a SuperAdmin user");
    }
    String password = createSpec.getPassword();
    if (confGetter.getGlobalConf(GlobalConfKeys.useOauth)) {
      if (confGetter.getGlobalConf(GlobalConfKeys.enableOidcAutoCreateUser)) {
        throw new PlatformServiceException(
            CONFLICT, "Users are created on their first OIDC login on this YBA");
      }
      // Users log in through OIDC only. A token would let the caller act as the user without it.
      if (Boolean.TRUE.equals(createSpec.getGenerateApiToken())) {
        throw new PlatformServiceException(
            BAD_REQUEST, "generate_api_token isn't allowed when OIDC SSO is enabled");
      }
      // The password is never used.
      password = Util.getRandomPassword();
    }
    passwordPolicyService.checkPasswordPolicy(cUUID, password);

    CreatedUser created = createUserAndToken(cUUID, spec, password, createSpec);
    Users user = created.user();
    String apiToken = created.apiToken();

    UserCreation creation = new UserCreation().user(UserMapper.INSTANCE.toV2User(user));
    if (apiToken != null) {
      creation.apiToken(apiToken).apiTokenVersion(user.getApiTokenVersion());
    }
    return creation;
  }

  private record CreatedUser(Users user, String apiToken) {}

  /** The user, its role bindings and its token are created together or not at all. */
  @Transactional
  private CreatedUser createUserAndToken(
      UUID cUUID, UserSpec spec, String password, UserCreateSpec createSpec) {
    // Throws CONFLICT for an email that is taken.
    Users user =
        Users.create(
            spec.getEmail(),
            password,
            Users.Role.valueOf(spec.getRole().toString()),
            cUUID,
            false /* isPrimary */);
    if (confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz)) {
      roleBindingUtil.createRoleBindingsForSystemRole(user);
    }
    String apiToken = null;
    if (Boolean.TRUE.equals(createSpec.getGenerateApiToken())) {
      apiToken = user.upsertApiToken();
    }
    return new CreatedUser(user, apiToken);
  }
}

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.rbac.Role;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import play.data.validation.Constraints;

@Data
public class OidcGroupToYbaRolesData {

  @Constraints.Required()
  @ApiModelProperty(value = " list of pairs of groupName and roles")
  private List<OidcGroupToYbaRolesPair> oidcGroupToYbaRolesPairs;

  public void validate() {
    oidcGroupToYbaRolesPairs.forEach(
        pair -> {
          if (pair.getRoles().isEmpty()) {
            throw new PlatformServiceException(BAD_REQUEST, "Roles cannot be empty!");
          }
          for (UUID roleUUID : pair.getRoles()) {
            Role r = Role.find.byId(roleUUID);
            if (r == null) {
              throw new PlatformServiceException(BAD_REQUEST, "Invalid role UUID!");
            }
            if (r.getName().equals("SuperAdmin")) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Cannot assign SuperAdmin role to groups!");
            }
          }
        });
  }

  @Data
  public static class OidcGroupToYbaRolesPair {
    @Constraints.Required() private String groupName;

    @Constraints.Required() private List<UUID> roles;
  }
}

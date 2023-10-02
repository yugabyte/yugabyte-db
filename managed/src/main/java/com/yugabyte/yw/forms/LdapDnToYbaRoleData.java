/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Users.Role;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@Getter
@Setter
@ApiModel(description = "A list of LDAP DN, YBA role pairs")
public class LdapDnToYbaRoleData {

  @Constraints.Required()
  @ApiModelProperty(value = " list of pairs of distinguishedName and ybaRole")
  private List<LdapDnYbaRoleDataPair> ldapDnToYbaRolePairs;

  public void validate() {
    for (LdapDnYbaRoleDataPair p : ldapDnToYbaRolePairs) {
      if (p.getYbaRole().equals(Role.SuperAdmin)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "SuperAdmin cannot be mapped to a disinguished name!");
      }
    }
  }

  @Getter
  @Setter
  @EqualsAndHashCode
  public static class LdapDnYbaRoleDataPair {
    @Constraints.Required() private String distinguishedName;

    @Constraints.Required() private Role ybaRole;
  }
}

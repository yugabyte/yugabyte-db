// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import lombok.Data;
import org.apache.commons.lang3.StringUtils;
import play.data.validation.Constraints;

@Data
public class DatabaseUserFormData {

  public String ycqlAdminUsername;
  public String ycqlAdminPassword;

  public String ysqlAdminUsername;
  public String ysqlAdminPassword;
  public String dbName;

  @Constraints.Required() public String username;

  @Constraints.Required() public String password;

  @ApiModelProperty(required = false, value = "YbaApi Internal.")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0")
  public List<RoleAttribute> dbRoleAttributes;

  @Data
  public static class RoleAttribute {
    private RoleAttributeName name;

    // Refer to the list of roleAttributes here:
    // https://www.postgresql.org/docs/current/sql-createrole.html
    public static enum RoleAttributeName {
      SUPERUSER,
      NOSUPERUSER,
      CREATEDB,
      NOCREATEDB,
      CREATEROLE,
      NOCREATEROLE,
      INHERIT,
      NOINHERIT,
      LOGIN,
      NOLOGIN,
      REPLICATION,
      NOREPLICATION,
      BYPASSRLS,
      NOBYPASSRLS
    }
  }

  // TODO(Shashank): Move this to use Validatable
  public void validation() {
    if (username == null || password == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Need to provide username and password.");
    }

    if (StringUtils.isEmpty(ysqlAdminUsername) && StringUtils.isEmpty(ycqlAdminUsername)) {
      throw new PlatformServiceException(BAD_REQUEST, "Need to provide YSQL and/or YCQL username.");
    }

    username = Util.removeEnclosingDoubleQuotes(username);

    // CHECK spotachev: this was not done before - I believe that was a bug (?)
    ysqlAdminUsername = Util.removeEnclosingDoubleQuotes(ysqlAdminUsername);
    ycqlAdminUsername = Util.removeEnclosingDoubleQuotes(ycqlAdminUsername);

    if (!StringUtils.isEmpty(ysqlAdminUsername)) {
      if (dbName == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "DB needs to be specified for YSQL user creation.");
      }

      if (username.contains("\"")) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid username.");
      }
    }
  }
}

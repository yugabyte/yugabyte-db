// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import org.apache.commons.lang3.StringUtils;
import play.data.validation.Constraints;

public class DatabaseUserFormData {

  public String ycqlAdminUsername;
  public String ycqlAdminPassword;

  public String ysqlAdminUsername;
  public String ysqlAdminPassword;
  public String dbName;

  @Constraints.Required() public String username;

  @Constraints.Required() public String password;

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

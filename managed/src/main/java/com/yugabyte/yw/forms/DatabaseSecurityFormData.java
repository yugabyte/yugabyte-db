// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.password.PasswordPolicyService;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import org.apache.commons.lang3.StringUtils;

@ApiModel(value = "DatabaseSecurityFormData", description = "Database security properties")
public class DatabaseSecurityFormData {

  @ApiModelProperty(value = "YCQL admin username")
  public String ycqlAdminUsername;

  @ApiModelProperty(value = "Current YCQL admin password")
  public String ycqlCurrAdminPassword;

  @ApiModelProperty(value = "New YCQL admin password")
  public String ycqlAdminPassword;

  @ApiModelProperty(value = "YSQL admin username")
  public String ysqlAdminUsername;

  @ApiModelProperty(value = "Current YSQL admin password")
  public String ysqlCurrAdminPassword;

  @ApiModelProperty(value = "New YSQL admin password")
  public String ysqlAdminPassword;

  @ApiModelProperty(value = "YSQL DB Name")
  public String dbName;

  // TODO(Shashank): Move this to use Validatable
  public void validation() {
    if (StringUtils.isEmpty(ysqlAdminUsername) && StringUtils.isEmpty(ycqlAdminUsername)) {
      throw new PlatformServiceException(BAD_REQUEST, "Need to provide YSQL and/or YCQL username.");
    }

    ysqlAdminUsername = Util.removeEnclosingDoubleQuotes(ysqlAdminUsername);
    ycqlAdminUsername = Util.removeEnclosingDoubleQuotes(ycqlAdminUsername);
    if (!StringUtils.isEmpty(ysqlAdminUsername)) {
      if (dbName == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "DB needs to be specified for YSQL user change.");
      }

      if (ysqlAdminUsername.contains("\"")) {
        throw new PlatformServiceException(BAD_REQUEST, "Invalid username.");
      }
    }

    ycqlCurrAdminPassword = Util.removeEnclosingDoubleQuotes(ycqlCurrAdminPassword);
    ycqlAdminPassword = Util.removeEnclosingDoubleQuotes(ycqlAdminPassword);
    if (!StringUtils.isEmpty(ycqlAdminPassword) && !StringUtils.isEmpty(ycqlCurrAdminPassword)) {
      if (StringUtils.equals(ycqlAdminPassword, ycqlCurrAdminPassword)) {
        throw new PlatformServiceException(BAD_REQUEST, "Please provide new YCQL password.");
      }
    }

    ysqlCurrAdminPassword = Util.removeEnclosingDoubleQuotes(ysqlCurrAdminPassword);
    ysqlAdminPassword = Util.removeEnclosingDoubleQuotes(ysqlAdminPassword);
    if (!StringUtils.isEmpty(ysqlAdminPassword) && !StringUtils.isEmpty(ysqlCurrAdminPassword)) {
      if (StringUtils.equals(ysqlAdminPassword, ysqlCurrAdminPassword)) {
        throw new PlatformServiceException(BAD_REQUEST, "Please provide new YSQL password.");
      }
    }
  }

  public void validatePassword(PasswordPolicyService policyService) {
    if (!StringUtils.isEmpty(ysqlAdminPassword)) {
      policyService.checkPasswordPolicy(null, ysqlAdminPassword);
    }
    if (!StringUtils.isEmpty(ycqlAdminPassword)) {
      policyService.checkPasswordPolicy(null, ycqlAdminPassword);
    }
  }
}

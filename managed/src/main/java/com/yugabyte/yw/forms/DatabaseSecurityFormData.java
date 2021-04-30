// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YWServiceException;
import org.apache.commons.lang3.StringUtils;

import static play.mvc.Http.Status.BAD_REQUEST;


public class DatabaseSecurityFormData {

  public String ycqlAdminUsername;
  public String ycqlCurrAdminPassword;
  public String ycqlAdminPassword;

  public String ysqlAdminUsername;
  public String ysqlCurrAdminPassword;
  public String ysqlAdminPassword;

  public String dbName;

  // TODO(Shashank): Move this to use Validatable
  public void validation() {
    if (StringUtils.isEmpty(ysqlAdminUsername)
      && StringUtils.isEmpty(ycqlAdminUsername)) {
      throw new YWServiceException(BAD_REQUEST, "Need to provide YSQL and/or YCQL username.");
    }

    ysqlAdminUsername = Util.removeEnclosingDoubleQuotes(ysqlAdminUsername);
    ycqlAdminUsername = Util.removeEnclosingDoubleQuotes(ycqlAdminUsername);
    if (!StringUtils.isEmpty(ysqlAdminUsername)) {
      if (dbName == null) {
        throw new YWServiceException(BAD_REQUEST,
          "DB needs to be specified for YSQL user change.");
      }

      if (ysqlAdminUsername.contains("\"")) {
        throw new YWServiceException(BAD_REQUEST, "Invalid username.");
      }
    }
  }

}

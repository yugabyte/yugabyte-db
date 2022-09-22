// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class CustomerLicenseFormData {

  public String licenseContent;

  @Constraints.Required() public String licenseType;
}

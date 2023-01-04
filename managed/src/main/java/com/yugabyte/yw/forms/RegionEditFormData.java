package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class RegionEditFormData {
  @Constraints.MaxLength(255)
  public String ybImage;

  public String securityGroupId;

  public String vnetName;
}

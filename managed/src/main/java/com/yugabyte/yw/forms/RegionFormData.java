// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for Region Data
 */
public class RegionFormData {
  @Constraints.Required()
  @Constraints.MaxLength(25)
  public String code;

  @Constraints.MaxLength(100)
  public String name;

  @Constraints.MaxLength(255)
  public String ybImage;

  public String hostVpcRegion;

  public String hostVpcId;

  public String destVpcId;

  public double latitude = 0.0;
  public double longitude = 0.0;
}

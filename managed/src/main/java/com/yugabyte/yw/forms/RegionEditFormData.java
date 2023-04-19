package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Region;
import play.data.validation.Constraints;

public class RegionEditFormData {
  @Constraints.MaxLength(255)
  public String ybImage;

  public String securityGroupId;

  public String vnetName;

  public static RegionEditFormData fromRegion(Region region) {
    RegionEditFormData result = new RegionEditFormData();
    result.securityGroupId = region.getSecurityGroupId();
    result.vnetName = region.getVnetName();
    result.ybImage = region.getYbImage();
    return result;
  }
}

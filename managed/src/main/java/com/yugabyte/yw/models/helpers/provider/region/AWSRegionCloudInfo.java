package com.yugabyte.yw.models.helpers.provider.region;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.HashMap;
import java.util.Map;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
public class AWSRegionCloudInfo implements CloudInfoInterface {

  @ApiModelProperty
  @JsonAlias("vnetName")
  public String vnet;

  @ApiModelProperty
  @JsonAlias("sg_id")
  public String securityGroupId;

  @ApiModelProperty(
      value =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.</b> Use"
              + " provider.imageBundle instead",
      accessMode = AccessMode.READ_WRITE)
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0")
  public String ybImage;

  @ApiModelProperty(
      value =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.</b> Use"
              + " provider.imageBundle instead",
      accessMode = AccessMode.READ_WRITE)
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0")
  public Architecture arch;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (securityGroupId != null) {
      envVars.put("securityGroupId", securityGroupId);
    }
    if (arch != null) {
      envVars.put("arch", arch.toString());
    }
    if (vnet != null) {
      envVars.put("vnet", vnet);
    }
    if (ybImage != null) {
      envVars.put("ybImage", ybImage);
    }

    return envVars;
  }

  @JsonIgnore
  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config) {
    return config;
  }

  @JsonIgnore
  public void withSensitiveDataMasked() {
    // pass
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    // Pass
  }
}

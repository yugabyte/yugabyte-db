package com.yugabyte.yw.models.helpers.provider.region;

import com.fasterxml.jackson.annotation.JsonIgnore;
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
public class GCPRegionCloudInfo implements CloudInfoInterface {

  @ApiModelProperty(
      value = "Deprecated since YBA version 2.20.0. Use provider.imageBundle instead",
      accessMode = AccessMode.READ_WRITE)
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0")
  public String ybImage;

  @ApiModelProperty(
      value = "The instance template to be used for nodes created in this region.",
      accessMode = AccessMode.READ_WRITE)
  public String instanceTemplate;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

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

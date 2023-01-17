package com.yugabyte.yw.models.helpers.provider.region;

import java.util.HashMap;
import java.util.Map;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;

import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
public class GCPRegionCloudInfo implements CloudInfoInterface {

  @ApiModelProperty(
      value = "The AMI to be used in this region.",
      accessMode = AccessMode.READ_WRITE)
  public String ybImage;

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
}

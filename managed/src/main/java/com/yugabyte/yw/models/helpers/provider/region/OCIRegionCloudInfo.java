package com.yugabyte.yw.models.helpers.provider.region;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import io.swagger.annotations.ApiModelProperty;
import java.util.HashMap;
import java.util.Map;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
public class OCIRegionCloudInfo implements CloudInfoInterface {
  @ApiModelProperty
  @JsonAlias("vnetName")
  public String vnet;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (vnet != null) {
      envVars.put("vnet", vnet);
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

package com.yugabyte.yw.models.helpers.provider.region.azs;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.HashMap;
import java.util.Map;

public class DefaultAZCloudInfo implements CloudInfoInterface {

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    return new HashMap<String, String>();
  }

  @JsonIgnore
  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config) {
    // Pass
    return new HashMap<String, String>();
  }

  @JsonIgnore
  public void withSensitiveDataMasked() {
    // Pass
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    // Pass
  }
}

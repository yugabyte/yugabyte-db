// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.Collections;
import java.util.Map;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class LocalCloudInfo implements CloudInfoInterface {

  private String yugabyteBinDir;

  private String ybcBinDir;

  private String dataHomeDir;

  @Override
  public Map<String, String> getEnvVars() {
    return Collections.emptyMap();
  }

  @Override
  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config) {
    return Collections.emptyMap();
  }

  @Override
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    // pass
  }

  @Override
  public void withSensitiveDataMasked() {
    // pass
  }
}

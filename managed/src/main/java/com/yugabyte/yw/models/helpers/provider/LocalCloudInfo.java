// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class LocalCloudInfo implements CloudInfoInterface {

  private String yugabyteBinDir;

  private String ybcBinDir;

  private String dataHomeDir;

  @JsonAlias("HOSTED_ZONE_ID")
  private String hostedZoneId;

  @Override
  public Map<String, String> getEnvVars() {
    Map<String, String> res = new HashMap<>();
    if (hostedZoneId != null) {
      res.put("HOSTED_ZONE_ID", hostedZoneId);
    }
    return res;
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

package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.CloudProviderHelper.EditableInUseProvider;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.HashMap;
import java.util.Map;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class OnPremCloudInfo implements CloudInfoInterface {

  private static final Map<String, String> configKeyMap =
      ImmutableMap.of("ybHomeDir", "YB_HOME_DIR");

  @JsonAlias("YB_HOME_DIR")
  @ApiModelProperty
  @EditableInUseProvider(name = "Yugabyte Home directory", allowed = false)
  public String ybHomeDir;

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.25.1.0")
  @ApiModelProperty(value = "WARNING: This is a preview API that could change.")
  @EditableInUseProvider(name = "Configure and use clockbound", allowed = true)
  public boolean useClockbound;

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1")
  @ApiModelProperty(value = "WARNING: This is a preview API that could change.")
  @EditableInUseProvider(name = "Enable multi-tenancy", allowed = true)
  public boolean enableMultiTenancy;

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1")
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Enable GCS-on-AWS cross-cloud"
              + " federated IAM on this provider's DB nodes (nodes must be AWS VMs).")
  @EditableInUseProvider(name = "Enable federated IAM", allowed = true)
  public boolean enableFederatedIam;

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1")
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. GCP Workload Identity Federation"
              + " audience used when federated IAM is enabled.")
  @EditableInUseProvider(name = "Federated IAM audience", allowed = true)
  public String federatedIamAudience;

  // Set by YNP when it creates the provider. YNP owns the configuration of such providers, so YBA
  // rejects user driven edits to them. It is internal in the sense that a user cannot flip it -
  // provider edit always carries over the value already persisted for the provider.
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2026.2.0")
  @ApiModelProperty(
      value = "YbaApi Internal. Provider is created and managed by YNP",
      accessMode = AccessMode.READ_ONLY)
  @EditableInUseProvider(name = "YNP managed", allowed = false)
  public boolean ynpManaged;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (ybHomeDir != null) {
      envVars.put("YB_HOME_DIR", ybHomeDir);
    }

    return envVars;
  }

  @JsonIgnore
  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config) {
    for (Map.Entry<String, String> entry : configKeyMap.entrySet()) {
      if (config.get(entry.getKey()) != null) {
        config.put(entry.getValue(), config.get(entry.getKey()));
        config.remove(entry.getKey());
      }
    }
    return config;
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

package com.yugabyte.yw.models.helpers.provider;

import java.util.HashMap;
import java.util.Map;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;

import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiParam;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class AzureCloudInfo implements CloudInfoInterface {

  private static final Map<String, String> configKeyMap =
      ImmutableMap.<String, String>builder()
          .put("azuTenantId", "AZURE_TENANT_ID")
          .put("azuClientId", "AZURE_CLIENT_ID")
          .put("azuClientSecret", "AZURE_CLIENT_SECRET")
          .put("azuSubscriptionId", "AZURE_SUBSCRIPTION_ID")
          .put("azuRG", "AZURE_RG")
          .put("azuHostedZoneId", "HOSTED_ZONE_ID")
          .build();

  @JsonAlias("AZURE_TENANT_ID")
  @ApiModelProperty
  public String azuTenantId;

  @JsonAlias("AZURE_CLIENT_ID")
  @ApiModelProperty
  public String azuClientId;

  @JsonAlias("AZURE_CLIENT_SECRET")
  @ApiModelProperty
  public String azuClientSecret;

  @JsonAlias("AZURE_SUBSCRIPTION_ID")
  @ApiModelProperty
  public String azuSubscriptionId;

  @JsonAlias("AZURE_RG")
  @ApiModelProperty
  public String azuRG;

  @JsonAlias("HOSTED_ZONE_ID")
  @ApiModelProperty
  @ApiParam(value = "Private DNS Zone")
  public String azuHostedZoneId;

  @JsonAlias("HOSTED_ZONE_NAME")
  @ApiModelProperty(hidden = true)
  public String azuHostedZoneName;

  @ApiModelProperty(
      value = "New/Existing VPC for provider creation",
      accessMode = AccessMode.READ_ONLY)
  private VPCType vpcType = VPCType.EXISTING;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (azuClientId != null) {
      envVars.put("AZURE_TENANT_ID", azuTenantId);
      envVars.put("AZURE_CLIENT_ID", azuClientId);
      envVars.put("AZURE_CLIENT_SECRET", azuClientSecret);
      envVars.put("AZURE_SUBSCRIPTION_ID", azuSubscriptionId);
      envVars.put("AZURE_RG", azuRG);
      if (azuHostedZoneId != null) {
        envVars.put("HOSTED_ZONE_ID", azuHostedZoneId);
      }
      if (azuHostedZoneName != null) {
        envVars.put("HOSTED_ZONE_NAME", azuHostedZoneName);
      }
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
    this.azuClientSecret = CommonUtils.getMaskedValue(azuClientSecret);
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    AzureCloudInfo azureCloudInfo = (AzureCloudInfo) providerCloudInfo;
    // If the modify request contains masked value, overwrite those using
    // the existing ebean entity.
    if (this.azuClientSecret != null && this.azuClientSecret.contains("*")) {
      this.azuClientSecret = azureCloudInfo.azuClientSecret;
    }
  }
}

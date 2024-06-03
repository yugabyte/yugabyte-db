package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.CloudProviderHelper.EditableInUseProvider;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import io.swagger.annotations.ApiParam;
import java.util.HashMap;
import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.lang3.StringUtils;
import play.data.validation.Constraints.Required;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
@Builder(toBuilder = true)
@AllArgsConstructor
@NoArgsConstructor
public class AzureCloudInfo implements CloudInfoInterface {

  private static final Map<String, String> configKeyMap =
      ImmutableMap.<String, String>builder()
          .put("azuTenantId", "AZURE_TENANT_ID")
          .put("azuClientId", "AZURE_CLIENT_ID")
          .put("azuClientSecret", "AZURE_CLIENT_SECRET")
          .put("azuSubscriptionId", "AZURE_SUBSCRIPTION_ID")
          .put("azuRG", "AZURE_RG")
          .put("azuHostedZoneId", "HOSTED_ZONE_ID")
          .put("azuNetworkRG", "AZURE_NETWORK_RG")
          .put("azuNetworkSubscriptionId", "AZURE_NETWORK_SUBSCRIPTION_ID")
          .build();

  @JsonAlias("AZURE_TENANT_ID")
  @ApiModelProperty
  @EditableInUseProvider(name = "Azure Tenant ID", allowed = false)
  @Required
  public String azuTenantId;

  @JsonAlias("AZURE_CLIENT_ID")
  @ApiModelProperty
  @EditableInUseProvider(name = "Azure Client ID", allowed = false)
  @Required
  public String azuClientId;

  @JsonAlias("AZURE_CLIENT_SECRET")
  @ApiModelProperty
  public String azuClientSecret;

  @JsonAlias("AZURE_SUBSCRIPTION_ID")
  @ApiModelProperty
  @EditableInUseProvider(name = "Azure Subscription ID", allowed = false)
  @Required
  public String azuSubscriptionId;

  @JsonAlias("AZURE_NETWORK_SUBSCRIPTION_ID")
  @ApiModelProperty
  @EditableInUseProvider(name = "Azure Network Subscription ID", allowed = false)
  public String azuNetworkSubscriptionId;

  @JsonAlias("AZURE_RG")
  @EditableInUseProvider(name = "Azure Resource Group", allowed = false)
  @ApiModelProperty
  @Required
  public String azuRG;

  @JsonAlias("AZURE_NETWORK_RG")
  @ApiModelProperty
  @EditableInUseProvider(name = "Azure Network Resource Group", allowed = false)
  public String azuNetworkRG;

  @JsonAlias("HOSTED_ZONE_ID")
  @ApiModelProperty
  @EditableInUseProvider(name = "Azure Hosted Zone ID", allowed = false)
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
      envVars.put("AZURE_SUBSCRIPTION_ID", azuSubscriptionId);
      envVars.put("AZURE_RG", azuRG);
      if (StringUtils.isNotBlank(azuClientSecret)) {
        envVars.put("AZURE_CLIENT_SECRET", azuClientSecret);
      }
      if (StringUtils.isNotBlank(azuNetworkRG)) {
        envVars.put("AZURE_NETWORK_RG", azuNetworkRG);
      }
      if (StringUtils.isNotBlank(azuNetworkSubscriptionId)) {
        envVars.put("AZURE_NETWORK_SUBSCRIPTION_ID", azuNetworkSubscriptionId);
      }
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

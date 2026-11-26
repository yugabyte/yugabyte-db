package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudProviderHelper.EditableInUseProvider;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import io.swagger.annotations.ApiParam;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class AWSCloudInfo implements CloudInfoInterface {

  private static final Map<String, String> configKeyMap =
      ImmutableMap.of(
          "awsAccessKeyID",
          "AWS_ACCESS_KEY_ID",
          "awsAccessKeySecret",
          "AWS_SECRET_ACCESS_KEY",
          "awsHostedZoneId",
          "HOSTED_ZONE_ID",
          "awsHostedZoneName",
          "HOSTED_ZONE_NAME");

  @JsonAlias("AWS_ACCESS_KEY_ID")
  @ApiModelProperty
  public String awsAccessKeyID;

  @JsonAlias("AWS_SECRET_ACCESS_KEY")
  @ApiModelProperty
  public String awsAccessKeySecret;

  @YBADeprecated(sinceYBAVersion = "2.20.3", sinceDate = "2024-04-10")
  @ApiModelProperty
  @EditableInUseProvider(name = "IMDSv2 Required", allowed = false)
  public Boolean useIMDSv2 = true;

  @JsonAlias("HOSTED_ZONE_ID")
  @ApiModelProperty
  @ApiParam(value = "Route 53 Zone ID")
  @EditableInUseProvider(name = "AWS Hosted Zone ID", allowed = false)
  public String awsHostedZoneId;

  @JsonAlias("HOSTED_ZONE_NAME")
  @ApiModelProperty
  @EditableInUseProvider(name = "AWS Hosted Zone Name", allowed = false)
  public String awsHostedZoneName;

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  @EditableInUseProvider(name = "AWS Host VPC Region", allowed = false)
  public String hostVpcRegion;

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  @EditableInUseProvider(name = "AWS Host VPC ID", allowed = false)
  public String hostVpcId;

  @ApiModelProperty(
      value = "New/Existing VPC for provider creation",
      accessMode = AccessMode.READ_ONLY)
  @EditableInUseProvider(name = "AWS VPC type", allowed = false)
  private VPCType vpcType = VPCType.EXISTING;

  @ApiModelProperty(
      value =
          "Enable GCS-on-AWS cross-cloud federated IAM on this provider's DB nodes (GCP Workload"
              + " Identity Federation). Requires the federated IAM audience below.")
  @EditableInUseProvider(name = "Enable federated IAM", allowed = true)
  public boolean enableFederatedIam;

  /**
   * @deprecated superseded by {@link #crossCloudFederationTargets}, which can name more than one
   *     storage cloud. Still read so providers configured before that list keep working.
   */
  @Deprecated
  @ApiModelProperty(
      value =
          "Deprecated: use crossCloudFederationTargets. GCP Workload Identity Federation audience"
              + " (//iam.googleapis.com/projects/.../providers/...), used when federated IAM is"
              + " enabled. The DB node renders the external_account credential from it.")
  @EditableInUseProvider(name = "Federated IAM audience", allowed = true)
  public String federatedIamAudience;

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.2.0")
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Storage clouds this provider's"
              + " DB nodes can be given federated access to, at most one entry per cloud. A node"
              + " uses the entry for a cloud it is not itself running on.")
  @EditableInUseProvider(name = "Federated IAM targets", allowed = true)
  public List<CrossCloudFederationTarget> crossCloudFederationTargets;

  @Override
  @JsonIgnore
  public boolean isFederatedIamEnabled() {
    return enableFederatedIam;
  }

  /** Every node is on AWS, so the legacy single audience could only ever have meant GCS. */
  @Override
  @JsonIgnore
  public List<CrossCloudFederationTarget> getEffectiveFederationTargets() {
    if (CollectionUtils.isNotEmpty(crossCloudFederationTargets)) {
      return crossCloudFederationTargets;
    }
    if (StringUtils.isBlank(federatedIamAudience)) {
      return Collections.emptyList();
    }
    return Collections.singletonList(
        CrossCloudFederationTarget.of(CloudType.gcp, federatedIamAudience, null));
  }

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (awsAccessKeyID != null) {
      envVars.put("AWS_ACCESS_KEY_ID", awsAccessKeyID);
      envVars.put("AWS_SECRET_ACCESS_KEY", awsAccessKeySecret);
    }
    if (awsHostedZoneId != null) {
      envVars.put("HOSTED_ZONE_ID", awsHostedZoneId);
    }
    if (awsHostedZoneName != null) {
      envVars.put("HOSTED_ZONE_NAME", awsHostedZoneName);
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
    this.awsAccessKeyID = CommonUtils.getMaskedValue(awsAccessKeyID);
    this.awsAccessKeySecret = CommonUtils.getMaskedValue(awsAccessKeySecret);
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    AWSCloudInfo awsCloudInfo = (AWSCloudInfo) providerCloudInfo;
    // If the modify request contains masked value, overwrite those using
    // the existing ebean entity.
    if (this.awsAccessKeyID != null && this.awsAccessKeyID.contains("*")) {
      this.awsAccessKeyID = awsCloudInfo.awsAccessKeyID;
    }
    if (this.awsAccessKeySecret != null && this.awsAccessKeySecret.contains("*")) {
      this.awsAccessKeySecret = awsCloudInfo.awsAccessKeySecret;
    }
    if (this.awsHostedZoneName == null && awsCloudInfo.awsHostedZoneName != null) {
      this.awsHostedZoneName = awsCloudInfo.awsHostedZoneName;
    }
  }
}

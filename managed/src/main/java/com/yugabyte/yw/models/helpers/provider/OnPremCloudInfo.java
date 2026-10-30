package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudProviderHelper.EditableInUseProvider;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CrossCloudFederationTarget;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
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
          "WARNING: This is a preview API that could change. Enable cross-cloud federated IAM on"
              + " this provider's DB nodes. Each node is given access to the storage clouds it is"
              + " not running on, per crossCloudFederationTargets.")
  @EditableInUseProvider(name = "Enable federated IAM", allowed = true)
  public boolean enableFederatedIam;

  /**
   * @deprecated superseded by {@link #crossCloudFederationTargets}. Still read so providers
   *     configured before this provider could reach more than one storage cloud keep working; new
   *     writes should use the list.
   */
  @Deprecated
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1")
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Deprecated: use"
              + " crossCloudFederationTargets. GCP Workload Identity Federation audience used when"
              + " federated IAM is enabled.")
  @EditableInUseProvider(name = "Federated IAM audience", allowed = true)
  public String federatedIamAudience;

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.2.0")
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. Storage clouds this provider's DB"
              + " nodes can reach with federated IAM, at most one entry per cloud. A node uses the"
              + " entry for a cloud it is not itself running on, and is left alone when there is"
              + " none - so add an entry per storage cloud the nodes must actually reach.")
  @EditableInUseProvider(name = "Federated IAM targets", allowed = true)
  public List<CrossCloudFederationTarget> crossCloudFederationTargets;

  /**
   * Configured targets, falling back to the flat audience that predates this list so a provider
   * configured before it keeps working untouched. That audience only ever meant GCS, which an
   * on-prem provider could reach solely from AWS nodes.
   */
  @Override
  @JsonIgnore
  public boolean isFederatedIamEnabled() {
    return enableFederatedIam;
  }

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

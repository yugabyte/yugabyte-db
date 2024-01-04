package com.yugabyte.yw.models.helpers.provider;

import static com.yugabyte.yw.common.RedactingService.SECRET_REPLACEMENT;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.common.CloudProviderHelper.EditableInUseProvider;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class GCPCloudInfo implements CloudInfoInterface {

  private static final String sharedVPCProjectKey = "GCE_SHARED_VPC_PROJECT";

  private static final Map<String, String> configKeyMap =
      ImmutableMap.of(
          "gceProject",
          "project_id",
          sharedVPCProjectKey,
          "GCE_HOST_PROJECT",
          "gceApplicationCredentialsPath",
          "GOOGLE_APPLICATION_CREDENTIALS",
          "destVpcId",
          "network",
          "ybFirewallTags",
          CloudProviderHelper.YB_FIREWALL_TAGS,
          "useHostVPC",
          "use_host_vpc");

  private static final List<String> toRemoveKeyFromConfig =
      ImmutableList.of("gceApplicationCredentials", "useHostCredentials");

  private static final Map<String, String> toAddKeysInConfig =
      ImmutableMap.<String, String>builder()
          .put("client_email", "GCE_EMAIL")
          .put("project_id", "GCE_PROJECT")
          .put("auth_provider_x509_cert_url", "auth_provider_x509_cert_url")
          .put("auth_uri", "auth_uri")
          .put("client_id", "client_id")
          .put("client_x509_cert_url", "client_x509_cert_url")
          .put("private_key", "private_key")
          .put("private_key_id", "private_key_id")
          .put("token_uri", "token_uri")
          .put("type", "type")
          .build();

  private static final List<String> toMaskFieldsInCreds =
      ImmutableList.of("private_key", "private_key_id");

  @JsonAlias({"project_id", GCPCloudImpl.GCE_PROJECT_PROPERTY})
  @ApiModelProperty
  @EditableInUseProvider(name = "GCP Project", allowed = false)
  private String gceProject;

  @JsonAlias({"host_project_id", "GCE_HOST_PROJECT"})
  @ApiModelProperty
  @EditableInUseProvider(name = "Shared VPC Project", allowed = false)
  private String sharedVPCProject;

  @JsonAlias({"config_file_path", GCPCloudImpl.GOOGLE_APPLICATION_CREDENTIALS_PROPERTY})
  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  private String gceApplicationCredentialsPath;

  @JsonAlias("config_file_contents")
  @ApiModelProperty
  private JsonNode gceApplicationCredentials;

  @JsonAlias({"network", GCPCloudImpl.CUSTOM_GCE_NETWORK_PROPERTY})
  @ApiModelProperty
  @EditableInUseProvider(name = "Destination VPC ID", allowed = false)
  private String destVpcId;

  @JsonAlias(CloudProviderHelper.YB_FIREWALL_TAGS)
  @EditableInUseProvider(name = "Firewall Tags", allowed = false)
  @ApiModelProperty
  private String ybFirewallTags;

  @JsonAlias("use_host_vpc")
  @EditableInUseProvider(name = "Switching Host VPC", allowed = false)
  @ApiModelProperty
  private Boolean useHostVPC;

  @JsonAlias("use_host_credentials")
  @ApiModelProperty
  private Boolean useHostCredentials;

  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  private String hostVpcId;

  @ApiModelProperty(
      value = "New/Existing VPC for provider creation",
      accessMode = AccessMode.READ_ONLY)
  private VPCType vpcType = VPCType.EXISTING;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (ybFirewallTags != null) {
      envVars.put(CloudProviderHelper.YB_FIREWALL_TAGS, ybFirewallTags);
    }
    if (gceProject != null) {
      envVars.put(GCPCloudImpl.GCE_PROJECT_PROPERTY, gceProject);
    }
    if (gceApplicationCredentialsPath != null) {
      envVars.put(
          GCPCloudImpl.GOOGLE_APPLICATION_CREDENTIALS_PROPERTY, gceApplicationCredentialsPath);
    }
    if (destVpcId != null) {
      envVars.put("destVpcId", destVpcId);
    }
    if (sharedVPCProject != null) {
      envVars.put(sharedVPCProjectKey, sharedVPCProject);
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

    for (String removeKey : toRemoveKeyFromConfig) {
      config.remove(removeKey);
    }

    if (gceApplicationCredentials == null) {
      return config;
    }
    ObjectNode credentialJSON = (ObjectNode) gceApplicationCredentials;
    for (Map.Entry<String, String> entry : toAddKeysInConfig.entrySet()) {
      if (credentialJSON.get(entry.getKey()) != null) {
        config.put(entry.getValue(), credentialJSON.get(entry.getKey()).toString());
      }
    }

    return config;
  }

  @JsonIgnore
  public void withSensitiveDataMasked() {
    this.gceApplicationCredentialsPath = CommonUtils.getMaskedValue(gceApplicationCredentialsPath);
    this.gceApplicationCredentials =
        CommonUtils.getMaskedValue(gceApplicationCredentials, toMaskFieldsInCreds);
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    GCPCloudInfo gcpCloudInfo = (GCPCloudInfo) providerCloudInfo;
    // If the modify request contains masked value, overwrite those using
    // the existing ebean entity.
    if (this.gceApplicationCredentialsPath != null
        && this.gceApplicationCredentialsPath.contains("*")) {
      this.gceApplicationCredentialsPath = gcpCloudInfo.gceApplicationCredentialsPath;
    }

    if (gceApplicationCredentials != null) {
      // If any of the fields in the cred is masked, copy those from the cred saved in bean.
      try {
        ObjectNode editCredNodeValue = (ObjectNode) this.gceApplicationCredentials;
        ObjectNode providerCredNodeValue = (ObjectNode) gcpCloudInfo.gceApplicationCredentials;

        for (String key : toMaskFieldsInCreds) {
          if (editCredNodeValue.has(key) && providerCredNodeValue != null) {
            String keyValue = editCredNodeValue.get(key).toString();
            if (keyValue.contains("*") && providerCredNodeValue.has(key)) {
              editCredNodeValue.put(key, providerCredNodeValue.get(key));
            }
          }
        }
        this.gceApplicationCredentials = editCredNodeValue;
      } catch (Exception e) {
        // In case error occured parsing the credentials fall back to saved creds in provider.
        if (this.gceApplicationCredentials.asText().equals(SECRET_REPLACEMENT)) {
          // For handling the case of read-modify-write.
          this.gceApplicationCredentials = gcpCloudInfo.gceApplicationCredentials;
        } else {
          throw e;
        }
      }
    }
  }
}

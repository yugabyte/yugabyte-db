/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

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
public class OCICloudInfo implements CloudInfoInterface {

  /** Supported authentication mechanisms for talking to the OCI provider APIs. */
  public enum OciAuthType {
    // API signing key: tenancy + user + fingerprint + private key.
    API_KEY,
    // OCI Instance Principal (YBA host is part of a dynamic group with a provider policy).
    INSTANCE_PRINCIPAL;

    public static OciAuthType fromString(String value) {
      if (StringUtils.isBlank(value)) {
        return API_KEY;
      }
      return valueOf(value.trim().toUpperCase());
    }
  }

  private static final Map<String, String> configKeyMap =
      ImmutableMap.<String, String>builder()
          .put("ociTenancyId", "OCI_TENANCY_ID")
          .put("ociUserId", "OCI_USER_ID")
          .put("ociFingerprint", "OCI_FINGERPRINT")
          .put("ociPrivateKeyContent", "OCI_PRIVATE_KEY_CONTENT")
          .put("ociCompartmentId", "OCI_COMPARTMENT_ID")
          .put("ociRegion", "OCI_REGION")
          .put("ociHostedZoneId", "HOSTED_ZONE_ID")
          .build();

  @ApiModelProperty(value = "OCI authentication type (API_KEY or INSTANCE_PRINCIPAL)")
  @EditableInUseProvider(name = "OCI Auth Type", allowed = false)
  public String ociAuthType;

  @JsonAlias("OCI_TENANCY_ID")
  @ApiModelProperty(value = "OCI Tenancy OCID")
  @EditableInUseProvider(name = "OCI Tenancy ID", allowed = false)
  public String ociTenancyId;

  @JsonAlias("OCI_USER_ID")
  @ApiModelProperty(value = "OCI User OCID")
  @EditableInUseProvider(name = "OCI User ID", allowed = false)
  public String ociUserId;

  @JsonAlias("OCI_FINGERPRINT")
  @ApiModelProperty(value = "OCI API Key Fingerprint")
  @EditableInUseProvider(name = "OCI Fingerprint", allowed = false)
  public String ociFingerprint;

  @JsonAlias("OCI_PRIVATE_KEY_CONTENT")
  @ApiModelProperty(value = "OCI API Private Key content (PEM format)")
  public String ociPrivateKeyContent;

  @JsonAlias("OCI_COMPARTMENT_ID")
  @ApiModelProperty(value = "OCI Compartment OCID")
  @EditableInUseProvider(name = "OCI Compartment ID", allowed = false)
  @Required
  public String ociCompartmentId;

  @JsonAlias("OCI_REGION")
  @ApiModelProperty(value = "Default OCI Region")
  @EditableInUseProvider(name = "OCI Region", allowed = true)
  @Required
  public String ociRegion;

  @JsonAlias("HOSTED_ZONE_ID")
  @ApiModelProperty(value = "OCI DNS Zone OCID for hosted zone")
  @EditableInUseProvider(name = "OCI Hosted Zone ID", allowed = false)
  public String ociHostedZoneId;

  @JsonAlias("HOSTED_ZONE_NAME")
  @ApiModelProperty(hidden = true)
  public String ociHostedZoneName;

  @ApiModelProperty(
      value = "New/Existing VCN for provider creation",
      accessMode = AccessMode.READ_ONLY)
  private VPCType vpcType = VPCType.EXISTING;

  @JsonIgnore
  public OciAuthType getOciAuthTypeEnum() {
    return OciAuthType.fromString(ociAuthType);
  }

  @JsonIgnore
  public boolean usesInstancePrincipal() {
    return getOciAuthTypeEnum() == OciAuthType.INSTANCE_PRINCIPAL;
  }

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (usesInstancePrincipal()) {
      envVars.put("OCI_AUTH_TYPE", OciAuthType.INSTANCE_PRINCIPAL.name());
    } else {
      if (ociTenancyId != null) {
        envVars.put("OCI_TENANCY_ID", ociTenancyId);
      }
      if (ociUserId != null) {
        envVars.put("OCI_USER_ID", ociUserId);
      }
      if (ociFingerprint != null) {
        envVars.put("OCI_FINGERPRINT", ociFingerprint);
      }
      if (StringUtils.isNotBlank(ociPrivateKeyContent)) {
        envVars.put("OCI_PRIVATE_KEY_CONTENT", ociPrivateKeyContent);
      }
    }
    if (ociCompartmentId != null) {
      envVars.put("OCI_COMPARTMENT_ID", ociCompartmentId);
    }
    if (StringUtils.isNotBlank(ociRegion)) {
      envVars.put("OCI_REGION", ociRegion);
    }
    if (StringUtils.isNotBlank(ociHostedZoneId)) {
      envVars.put("HOSTED_ZONE_ID", ociHostedZoneId);
    }
    if (StringUtils.isNotBlank(ociHostedZoneName)) {
      envVars.put("HOSTED_ZONE_NAME", ociHostedZoneName);
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
    this.ociPrivateKeyContent = CommonUtils.getMaskedValue(ociPrivateKeyContent);
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    OCICloudInfo ociCloudInfo = (OCICloudInfo) providerCloudInfo;
    if (this.ociPrivateKeyContent != null && this.ociPrivateKeyContent.contains("*")) {
      this.ociPrivateKeyContent = ociCloudInfo.ociPrivateKeyContent;
    }
  }
}

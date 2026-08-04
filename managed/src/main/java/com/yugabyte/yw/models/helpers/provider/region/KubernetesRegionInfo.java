package com.yugabyte.yw.models.helpers.provider.region;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.Map;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@Data
@EqualsAndHashCode(callSuper = true)
@ToString(callSuper = true)
public class KubernetesRegionInfo extends KubernetesInfo {

  @JsonAlias("KUBENAMESPACE")
  @ApiModelProperty
  private String kubeNamespace;

  @JsonAlias("OVERRIDES")
  @ApiModelProperty
  private String overrides;

  @JsonAlias("KUBE_POD_ADDRESS_TEMPLATE")
  @ApiModelProperty
  private String kubePodAddressTemplate;

  @JsonAlias("KUBE_DOMAIN")
  @ApiModelProperty
  private String kubeDomain;

  @JsonAlias("CERT-MANAGER-ISSUER-KIND")
  @ApiModelProperty
  private String certManagerIssuerKind;

  @JsonAlias("CERT-MANAGER-ISSUER-NAME")
  @ApiModelProperty
  private String certManagerIssuerName;

  @JsonAlias("CERT-MANAGER-ISSUER-GROUP")
  @ApiModelProperty
  private String certManagerIssuerGroup;

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.3.0")
  @JsonAlias("CERT-MANAGER-CLUSTERISSUER")
  @ApiModelProperty(
      value =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.3.0.</b>. Use"
              + " certManagerIssuerKind and certManagerIssuerName instead",
      accessMode = AccessMode.READ_WRITE)
  private String certManagerClusterIssuer;

  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.3.0")
  @JsonAlias("CERT-MANAGER-ISSUER")
  @ApiModelProperty(
      value =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.3.0.</b>. Use"
              + " certManagerIssuerKind and certManagerIssuerName instead",
      accessMode = AccessMode.READ_WRITE)
  private String certManagerIssuer;

  @Override
  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = super.getEnvVars();

    if (overrides != null) {
      envVars.put("OVERRIDES", overrides);
    }
    if (kubeNamespace != null) {
      envVars.put("KUBENAMESPACE", kubeNamespace);
    }
    if (kubePodAddressTemplate != null) {
      envVars.put("KUBE_POD_ADDRESS_TEMPLATE", kubePodAddressTemplate);
    }
    if (kubeDomain != null) {
      envVars.put("KUBE_DOMAIN", kubeDomain);
    }
    String issuerKind =
        certManagerIssuerKind != null
            ? certManagerIssuerKind
            : certManagerClusterIssuer != null
                ? WellKnownIssuerKind.CLUSTER_ISSUER
                : certManagerIssuer != null ? WellKnownIssuerKind.ISSUER : null;

    String issuerName =
        certManagerIssuerName != null
            ? certManagerIssuerName
            : certManagerClusterIssuer != null
                ? certManagerClusterIssuer
                : certManagerIssuer != null ? certManagerIssuer : null;

    if (issuerKind != null) {
      envVars.put("CERT-MANAGER-ISSUER-KIND", issuerKind);
    }
    if (issuerName != null) {
      envVars.put("CERT-MANAGER-ISSUER-NAME", issuerName);
    }
    if (certManagerIssuerGroup != null) {
      envVars.put("CERT-MANAGER-ISSUER-GROUP", certManagerIssuerGroup);
    }
    return envVars;
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    super.mergeMaskedFields(providerCloudInfo);
  }
}

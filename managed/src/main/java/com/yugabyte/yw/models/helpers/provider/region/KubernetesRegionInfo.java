package com.yugabyte.yw.models.helpers.provider.region;

import java.util.Map;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;

import io.swagger.annotations.ApiModelProperty;

public class KubernetesRegionInfo extends KubernetesInfo {

  @JsonAlias("KUBENAMESPACE")
  @ApiModelProperty
  public String kubeNamespace;

  @JsonAlias("OVERRIDES")
  @ApiModelProperty
  public String overrides;

  @JsonAlias("KUBE_POD_ADDRESS_TEMPLATE")
  @ApiModelProperty
  public String kubePodAddressTemplate;

  @JsonAlias("KUBE_DOMAIN")
  @ApiModelProperty
  public String kubeDomain;

  @JsonAlias("CERT-MANAGER-CLUSTERISSUER")
  @ApiModelProperty
  public String certManagerClusterIssuer;

  @JsonAlias("CERT-MANAGER-ISSUER")
  @ApiModelProperty
  public String certManagerIssuer;

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
    if (certManagerClusterIssuer != null) {
      envVars.put("CERT-MANAGER-CLUSTERISSUER", certManagerClusterIssuer);
    }
    if (certManagerIssuer != null) {
      envVars.put("CERT-MANAGER-ISSUER", certManagerIssuer);
    }

    return envVars;
  }
}

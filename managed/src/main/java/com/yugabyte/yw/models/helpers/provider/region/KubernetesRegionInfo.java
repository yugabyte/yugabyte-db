package com.yugabyte.yw.models.helpers.provider.region;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.swagger.annotations.ApiModelProperty;
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

  @JsonAlias("CERT-MANAGER-CLUSTERISSUER")
  @ApiModelProperty
  private String certManagerClusterIssuer;

  @JsonAlias("CERT-MANAGER-ISSUER")
  @ApiModelProperty
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
    if (certManagerClusterIssuer != null) {
      envVars.put("CERT-MANAGER-CLUSTERISSUER", certManagerClusterIssuer);
    }
    if (certManagerIssuer != null) {
      envVars.put("CERT-MANAGER-ISSUER", certManagerIssuer);
    }

    return envVars;
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    super.mergeMaskedFields(providerCloudInfo);
  }
}

package com.yugabyte.yw.models.helpers.provider;

import java.util.HashMap;
import java.util.Map;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;

import io.swagger.annotations.ApiModelProperty;

import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class KubernetesInfo implements CloudInfoInterface {

  @JsonAlias("KUBECONFIG_PROVIDER")
  @ApiModelProperty
  private String kubernetesProvider;

  @YBADeprecated(sinceDate = "2023-03-8", sinceYBAVersion = "2.17.3.0")
  @JsonAlias("KUBECONFIG_SERVICE_ACCOUNT")
  @ApiModelProperty(value = "DEPRECATED: kubernetes service account is not needed.")
  private String kubernetesServiceAccount;

  @JsonAlias("KUBECONFIG_IMAGE_REGISTRY")
  @ApiModelProperty
  private String kubernetesImageRegistry;

  @JsonAlias("KUBECONFIG_IMAGE_PULL_SECRET_NAME")
  @ApiModelProperty
  private String kubernetesImagePullSecretName;

  @JsonAlias("KUBECONFIG_PULL_SECRET")
  @ApiModelProperty
  private String kubernetesPullSecret;

  @JsonAlias("KUBECONFIG_NAME")
  @ApiModelProperty
  private String kubeConfigName;

  @JsonAlias("KUBECONFIG_CONTENT")
  @ApiModelProperty
  private String kubeConfigContent;

  /**
   * Valid values include a file path (file can be empty) and an empty string (for using in-cluster
   * credentials).
   */
  @JsonAlias("KUBECONFIG")
  @ApiModelProperty
  private String kubeConfig;

  @JsonAlias("STORAGE_CLASS")
  @ApiModelProperty
  private String kubernetesStorageClass;

  @JsonAlias("KUBECONFIG_PULL_SECRET_CONTENT")
  @ApiModelProperty
  private String kubernetesPullSecretContent;

  @JsonAlias("KUBECONFIG_PULL_SECRET_NAME")
  @ApiModelProperty
  private String kubernetesPullSecretName;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (kubeConfig != null) {
      envVars.put("KUBECONFIG", kubeConfig);
    }
    if (kubeConfigName != null) {
      envVars.put("KUBECONFIG_NAME", kubeConfigName);
    }
    if (kubeConfigContent != null) {
      envVars.put("KUBECONFIG_CONTENT", kubeConfigContent);
    }
    if (kubernetesServiceAccount != null) {
      envVars.put("KUBECONFIG_SERVICE_ACCOUNT", kubernetesServiceAccount);
    }
    if (kubernetesProvider != null) {
      envVars.put("KUBECONFIG_PROVIDER", kubernetesProvider);
    }
    if (kubernetesStorageClass != null) {
      envVars.put("STORAGE_CLASS", kubernetesStorageClass);
    }
    if (kubernetesPullSecretName != null) {
      envVars.put("KUBECONFIG_PULL_SECRET_NAME", kubernetesPullSecretName);
    }
    if (kubernetesPullSecretContent != null) {
      envVars.put("KUBECONFIG_PULL_SECRET_CONTENT", kubernetesPullSecretContent);
    }
    if (kubernetesPullSecret != null) {
      envVars.put("KUBECONFIG_PULL_SECRET", kubernetesPullSecret);
    }
    if (kubernetesImagePullSecretName != null) {
      envVars.put("KUBECONFIG_IMAGE_PULL_SECRET_NAME", kubernetesImagePullSecretName);
    }
    if (kubernetesImageRegistry != null) {
      envVars.put("KUBECONFIG_IMAGE_REGISTRY", kubernetesImageRegistry);
    }
    return envVars;
  }

  @JsonIgnore
  public Map<String, String> getConfigMapForUIOnlyAPIs(Map<String, String> config) {
    return config;
  }

  @JsonIgnore
  public void withSensitiveDataMasked() {
    this.kubernetesImagePullSecretName = CommonUtils.getMaskedValue(kubernetesImagePullSecretName);
    this.kubernetesPullSecret = CommonUtils.getMaskedValue(kubernetesPullSecret);
    this.kubernetesPullSecretName = CommonUtils.getMaskedValue(kubernetesPullSecretName);
  }

  @JsonIgnore
  public void mergeMaskedFields(CloudInfoInterface providerCloudInfo) {
    KubernetesInfo kubernetesInfo = (KubernetesInfo) providerCloudInfo;
    // If the modify request contains masked value, overwrite those using
    // the existing ebean entity.
    if (this.kubernetesImagePullSecretName != null
        && this.kubernetesImagePullSecretName.contains("*")) {
      this.kubernetesImagePullSecretName = kubernetesInfo.kubernetesImagePullSecretName;
    }
    if (this.kubernetesPullSecret != null && this.kubernetesPullSecret.contains("*")) {
      this.kubernetesPullSecret = kubernetesInfo.kubernetesPullSecret;
    }
    if (this.kubernetesPullSecretName != null && this.kubernetesPullSecretName.contains("*")) {
      this.kubernetesPullSecretName = kubernetesInfo.kubernetesPullSecretName;
    }
  }
}

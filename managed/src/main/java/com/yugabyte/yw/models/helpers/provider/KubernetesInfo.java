package com.yugabyte.yw.models.helpers.provider;

import java.util.HashMap;
import java.util.Map;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
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
  public String kubernetesProvider;

  @JsonAlias("KUBECONFIG_SERVICE_ACCOUNT")
  @ApiModelProperty
  public String kubernetesServiceAccount;

  @JsonAlias("KUBECONFIG_IMAGE_REGISTRY")
  @ApiModelProperty
  public String kubernetesImageRegistry;

  @JsonAlias("KUBECONFIG_IMAGE_PULL_SECRET_NAME")
  @ApiModelProperty
  public String kubernetesImagePullSecretName;

  @JsonAlias("KUBECONFIG_PULL_SECRET")
  @ApiModelProperty
  public String kubernetesPullSecret;

  @JsonAlias("KUBECONFIG_NAME")
  @ApiModelProperty
  public String kubeConfigName;

  @JsonAlias("KUBECONFIG_CONTENT")
  @ApiModelProperty
  public String kubeConfigContent;

  @JsonAlias("KUBECONFIG")
  @ApiModelProperty
  public String kubeConfig;

  @JsonAlias("STORAGE_CLASS")
  @ApiModelProperty
  public String kubernetesStorageClass;

  @JsonAlias("KUBECONFIG_PULL_SECRET_CONTENT")
  @ApiModelProperty
  public String kubernetesPullSecretContent;

  @JsonAlias("KUBECONFIG_PULL_SECRET_NAME")
  @ApiModelProperty
  public String kubernetesPullSecretName;

  @JsonIgnore
  public Map<String, String> getEnvVars() {
    Map<String, String> envVars = new HashMap<>();

    if (kubeConfig != null) {
      envVars.put("KUBECONFIG", kubeConfig);
    }

    if (kubeConfigName != null) {
      envVars.put("KUBECONFIG_NAME", kubeConfigName);
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
}

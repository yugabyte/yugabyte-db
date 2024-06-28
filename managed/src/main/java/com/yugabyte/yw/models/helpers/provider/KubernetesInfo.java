package com.yugabyte.yw.models.helpers.provider;

import com.fasterxml.jackson.annotation.*;
import com.yugabyte.yw.common.CloudProviderHelper.EditableInUseProvider;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import java.util.HashMap;
import java.util.Map;
import lombok.Data;

@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class KubernetesInfo implements CloudInfoInterface {

  @JsonAlias("KUBECONFIG_PROVIDER")
  @EditableInUseProvider(name = "Kubernetes Provider", allowed = false)
  @ApiModelProperty
  private String kubernetesProvider;

  @YBADeprecated(sinceDate = "2023-03-8", sinceYBAVersion = "2.17.3.0")
  @JsonAlias("KUBECONFIG_SERVICE_ACCOUNT")
  @EditableInUseProvider(name = "Kubernetes Service Account", allowed = false)
  @ApiModelProperty(value = "DEPRECATED: kubernetes service account is not needed.")
  private String kubernetesServiceAccount;

  @JsonAlias("KUBECONFIG_IMAGE_REGISTRY")
  @ApiModelProperty
  @EditableInUseProvider(name = "Kubernetes Image Registry", allowed = false)
  private String kubernetesImageRegistry;

  @JsonAlias("KUBECONFIG_IMAGE_PULL_SECRET_NAME")
  @EditableInUseProvider(name = "Kubernetes Image Pull Secret Name", allowed = false)
  @ApiModelProperty
  private String kubernetesImagePullSecretName;

  @JsonAlias("KUBECONFIG_PULL_SECRET")
  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  private String kubernetesPullSecret;

  @JsonAlias("KUBECONFIG_NAME")
  @ApiModelProperty
  private String kubeConfigName;

  @JsonAlias("KUBECONFIG_CONTENT")
  @ApiModelProperty
  private String kubeConfigContent;

  @JsonAlias("KUBECONFIG")
  @ApiModelProperty(accessMode = AccessMode.READ_ONLY)
  private String kubeConfig;

  @JsonAlias("STORAGE_CLASS")
  @EditableInUseProvider(name = "Kubernetes Storage Class", allowed = false)
  @ApiModelProperty
  private String kubernetesStorageClass;

  @JsonAlias("KUBECONFIG_PULL_SECRET_CONTENT")
  @ApiModelProperty
  @EditableInUseProvider(name = "Kubernetes Pull Secret", allowed = false)
  private String kubernetesPullSecretContent;

  @JsonAlias("KUBECONFIG_PULL_SECRET_NAME")
  @ApiModelProperty
  @EditableInUseProvider(name = "Kubernetes Pull Secret Name", allowed = false)
  private String kubernetesPullSecretName;

  @JsonProperty("isKubernetesOperatorControlled")
  // Flag for identifying the legacy k8s providers created before release 2.18.
  @ApiModelProperty(hidden = true)
  private boolean legacyK8sProvider = true;

  // TODO(bhavin192): Should caFile, tokenFile, apiServerEndpoint, and
  // kubeconfig be part of a subclass named kubeConfigInfo?

  @ApiModelProperty(hidden = true)
  private String kubeConfigCAFile;

  @ApiModelProperty(hidden = true)
  private String kubeConfigTokenFile;

  @ApiModelProperty(hidden = true)
  private String apiServerEndpoint;

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
    // Fields not accessible to the user
    this.kubeConfigCAFile = null;
    this.kubeConfigTokenFile = null;
    this.apiServerEndpoint = null;
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
    // We don't want to change it once created.
    this.legacyK8sProvider = kubernetesInfo.legacyK8sProvider;
    // Restore fields not accessible to the user
    this.kubeConfigCAFile = kubernetesInfo.kubeConfigCAFile;
    this.kubeConfigTokenFile = kubernetesInfo.kubeConfigTokenFile;
    this.apiServerEndpoint = kubernetesInfo.apiServerEndpoint;
  }
}

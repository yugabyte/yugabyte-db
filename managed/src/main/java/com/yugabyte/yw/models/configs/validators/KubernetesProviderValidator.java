// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.google.common.collect.SetMultimap;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import io.fabric8.kubernetes.api.model.storage.StorageClass;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Singleton
public class KubernetesProviderValidator extends ProviderFieldsValidator {

  private final RuntimeConfGetter confGetter;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  public KubernetesProviderValidator(
      BeanValidator beanValidator,
      RuntimeConfGetter runtimeConfGetter,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(beanValidator, runtimeConfGetter);
    this.confGetter = runtimeConfGetter;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Override
  public void validate(Provider provider) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableK8sProviderValidation)) {
      log.info("Kubernetes provider validation is not enabled");
      return;
    }

    JsonNode providerJson = Json.toJson(provider);
    JsonNode providerWithPath = Util.addJsonPathToLeafNodes(providerJson);

    Path tmpDir =
        FileUtils.getOrCreateTmpDirectory(
            confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath));
    // Create provider specific temporary directory to store files
    // like kubeconfigs.
    Path providerTmpDir = null;
    try {
      providerTmpDir = Files.createTempDirectory(tmpDir, provider.getName());
    } catch (IOException e) {
      throw new RuntimeException("Failed to create temporary directory", e);
    }

    SetMultimap<String, String> validationErrors = HashMultimap.create();

    try {
      // TODO: do the non-zone specific validations here.

      Map<String, Map<String, JsonNode>> azToConfig =
          getCloudInfoPerAZ(providerWithPath, providerTmpDir);
      for (Map.Entry<String, Map<String, JsonNode>> entry : azToConfig.entrySet()) {
        String zone = entry.getKey();
        Map<String, JsonNode> k8sInfo = entry.getValue();
        validateZoneLabels(k8sInfo, validationErrors);
        validateStorageClass(k8sInfo, validationErrors);
        validateNamespace(k8sInfo, validationErrors);
        validateCertManagerIssuer(k8sInfo, validationErrors);
        validateMCSAddressTemplate(k8sInfo, validationErrors);
      }
    } finally {
      FileUtils.deleteDirectory(providerTmpDir.toFile());
    }

    if (!validationErrors.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrors, providerJson);
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // pass. We don't have any zone only validation.
  }

  // Returns a merged config for each AZ. Uses the given dir to store
  // any field content as a file on disk.
  private Map<String, Map<String, JsonNode>> getCloudInfoPerAZ(JsonNode provider, Path dir) {
    Map<String, Map<String, JsonNode>> azToConfig = new HashMap<>();
    Map<String, JsonNode> providerConfig = getKubernetesInfo(provider);
    writeKubeconfigToFile(providerConfig, dir);
    provider
        .get("regions")
        .elements()
        .forEachRemaining(
            region -> {
              String regionCode = getStringValue(region.get("code"));
              Map<String, JsonNode> regionConfig = getKubernetesInfo(region);
              writeKubeconfigToFile(regionConfig, dir);
              region
                  .get("zones")
                  .elements()
                  .forEachRemaining(
                      zone -> {
                        JsonNode zoneCodeNode = zone.get("code");
                        String zoneCode = getStringValue(zoneCodeNode);
                        Map<String, JsonNode> zoneConfig = getKubernetesInfo(zone);
                        writeKubeconfigToFile(zoneConfig, dir);

                        Map<String, JsonNode> config = new HashMap<>();
                        config.putAll(providerConfig);
                        config.putAll(regionConfig);
                        config.putAll(zoneConfig);
                        config.put("zoneCode", zoneCodeNode);

                        // New providers have kubeConfigContent and
                        // during edit we have kubeConfig if the
                        // kubeconfig is unchanged.
                        if (!config.containsKey("kubeConfigContent")
                            && !config.containsKey("kubeConfig")) {
                          log.warn(
                              "No kubeconfig found at any level, using in-cluster credentials");
                          config.put(
                              "kubeConfig",
                              // TODO: the jsonPath could be different
                              // like $.in-cluster-credentials?
                              Json.newObject().put("jsonPath", "$").put("value", ""));
                        }

                        azToConfig.put(regionCode + "." + zoneCode, config);
                      });
            });
    return azToConfig;
  }

  // Extracts and converts details.cloudInfo.kubernetes from given
  // JsonNode. node can be provider, region, or zone.
  private Map<String, JsonNode> getKubernetesInfo(JsonNode node) {
    return Json.mapper()
        .convertValue(
            node.get("details").get("cloudInfo").get("kubernetes"),
            new TypeReference<Map<String, JsonNode>>() {});
  }

  // Write the kubeconfig content to a temporary file in the given
  // directory. Replaces the kubeconfig content with the file path.
  private void writeKubeconfigToFile(Map<String, JsonNode> config, Path dir) {
    JsonNode kubeconfigNode = config.get("kubeConfigContent");
    if (kubeconfigNode == null) {
      return;
    }
    String kubeconfigName = "kubeconfig";
    JsonNode kubeconfigNameNode = config.get("kubeConfigName");
    if (kubeconfigNameNode != null) {
      kubeconfigName = getStringValue(kubeconfigNameNode);
    }

    File kubeconfigFile = null;
    try {
      kubeconfigFile = Files.createTempFile(dir, kubeconfigName, ".yaml").toFile();
      kubeconfigFile.deleteOnExit();
      FileUtils.writeStringToFile(kubeconfigFile, getStringValue(kubeconfigNode));
    } catch (Exception e) {
      throw new RuntimeException("Failed to write kubeconfig content to disk", e);
    }
    ((ObjectNode) kubeconfigNode).put("value", kubeconfigFile.getPath());
  }

  private void validateZoneLabels(
      Map<String, JsonNode> k8sInfo, SetMultimap<String, String> validationErrors) {
    Multimap<String, String> regionToZone = null;
    try {
      regionToZone =
          KubernetesUtil.computeKubernetesRegionToZoneInfo(
              ImmutableMap.of("KUBECONFIG", getStringValue(getKubeConfigNode(k8sInfo))),
              kubernetesManagerFactory);
    } catch (RuntimeException e) {
      if (e.getMessage().contains("Error from server (Forbidden): nodes")) {
        log.warn("Unable to validate zone label: {}", e.getMessage());
        return;
      }
      throw e;
    }

    JsonNode zoneCodeNode = k8sInfo.get("zoneCode");
    String zone = getStringValue(zoneCodeNode);
    if (!regionToZone.containsValue(zone)) {
      validationErrors.put(
          getJsonPath(zoneCodeNode), "Cluster doesn't have any nodes in the " + zone + " zone");
    }
  }

  private void validateStorageClass(
      Map<String, JsonNode> k8sInfo, SetMultimap<String, String> validationErrors) {
    // TODO(bhavin192): check the default storage class if a storage
    // class is not provided. This can be part of async validation as
    // well.
    JsonNode storageClassNode = k8sInfo.get("kubernetesStorageClass");
    if (storageClassNode == null) {
      return;
    }
    String storageClassJsonPath = getJsonPath(storageClassNode);

    StorageClass sc = null;
    try {
      sc =
          kubernetesManagerFactory
              .getManager()
              .getStorageClass(
                  ImmutableMap.of("KUBECONFIG", getStringValue(getKubeConfigNode(k8sInfo))),
                  getStringValue(storageClassNode));
    } catch (RuntimeException e) {
      if (e.getMessage().contains("Error from server (NotFound): storageclasses")) {
        validationErrors.put(storageClassJsonPath, "Storage class doesn't exist in the cluster");
        return;
      }
      if (e.getMessage().contains("Error from server (Forbidden): storageclasses")) {
        log.warn("Unable to validate storage class: {}", e.getMessage());
        return;
      }
      throw e;
    }

    if (!sc.getVolumeBindingMode().equalsIgnoreCase("WaitForFirstConsumer")) {
      validationErrors.put(
          storageClassJsonPath,
          "Storage class volumeBindingMode is not set to 'WaitForFirstConsumer'");
    }
    // Treating null as false.
    if (!Objects.requireNonNullElse(sc.getAllowVolumeExpansion(), false)) {
      validationErrors.put(
          storageClassJsonPath, "Storage class doesn't have allowVolumeExpansion set to true");
    }
  }

  private void validateNamespace(
      Map<String, JsonNode> k8sInfo, SetMultimap<String, String> validationErrors) {
    JsonNode namespaceNode = k8sInfo.get("kubeNamespace");
    if (namespaceNode == null) {
      return;
    }

    boolean nsExists = false;
    try {
      nsExists =
          kubernetesManagerFactory
              .getManager()
              .dbNamespaceExists(
                  ImmutableMap.of("KUBECONFIG", getStringValue(getKubeConfigNode(k8sInfo))),
                  getStringValue(namespaceNode));
    } catch (RuntimeException e) {
      if (e.getMessage().contains("error: failed to create secret secrets is forbidden")
          || e.getMessage().contains("Error from server (Forbidden): secrets is forbidden")) {
        validationErrors.put(
            getJsonPath(getKubeConfigNode(k8sInfo)),
            "Missing permission to create secrets in " + getStringValue(namespaceNode));
        return;
      }
      throw e;
    }
    if (!nsExists) {
      validationErrors.put(getJsonPath(namespaceNode), "Namespace doesn't exist in the cluster");
    }
  }

  private void validateCertManagerIssuer(
      Map<String, JsonNode> k8sInfo, SetMultimap<String, String> validationErrors) {
    // certManagerIssuer and certManagerClusterIssuer both can be provided, but the clusterIssuer is
    // given preference in KubernetesCommandExecutor.

    Map<String, String> config =
        ImmutableMap.of("KUBECONFIG", getStringValue(getKubeConfigNode(k8sInfo)));

    JsonNode clusterIssuerNode = k8sInfo.get("certManagerClusterIssuer");
    if (clusterIssuerNode != null) {
      try {
        if (!kubernetesManagerFactory
            .getManager()
            .resourceExists(
                config,
                "clusterissuers.cert-manager.io",
                getStringValue(clusterIssuerNode),
                null)) {
          validationErrors.put(
              getJsonPath(clusterIssuerNode), "ClusterIssuer doesn't exist in the cluster");
        }
      } catch (RuntimeException e) {
        if (e.getMessage().contains("error: the server doesn't have a resource type")) {
          validationErrors.put(
              getJsonPath(clusterIssuerNode),
              "Cluster doesn't have ClusterIssuer resource type, ensure cert-manager is installed");
        } else if (e.getMessage().contains("Error from server (Forbidden): clusterissuers")) {
          log.warn("Unable to validate cert-manager ClusterIssuer: {}", e.getMessage());
        } else {
          throw e;
        }
      }
    }

    JsonNode issuerNode = k8sInfo.get("certManagerIssuer");
    if (issuerNode != null) {
      JsonNode namespaceNode = k8sInfo.get("kubeNamespace");
      if (namespaceNode == null) {
        validationErrors.put(
            getJsonPath(issuerNode), "Namespace must be provided when using Issuer");
        return;
      }
      try {
        String namespace = getStringValue(namespaceNode);
        if (!kubernetesManagerFactory
            .getManager()
            .resourceExists(
                config, "issuers.cert-manager.io", getStringValue(issuerNode), namespace)) {
          validationErrors.put(
              getJsonPath(issuerNode), "Issuer doesn't exist in the " + namespace + " namespace");
        }
      } catch (RuntimeException e) {
        if (e.getMessage().contains("error: the server doesn't have a resource type")) {
          validationErrors.put(
              getJsonPath(issuerNode),
              "Cluster doesn't have Issuer resource type, ensure cert-manager is installed");
        } else if (e.getMessage().contains("Error from server (NotFound): namespaces")) {
          validationErrors.put(
              getJsonPath(namespaceNode), "Namespace doesn't exist in the cluster");
        } else if (e.getMessage().contains("Error from server (Forbidden): issuers")) {
          log.warn("Unable to validate cert-manager Issuer: {}", e.getMessage());
        } else {
          throw e;
        }
      }
    }
  }

  private void validateMCSAddressTemplate(
      Map<String, JsonNode> k8sInfo, SetMultimap<String, String> validationErrors) {
    JsonNode podAddressNode = k8sInfo.get("kubePodAddressTemplate");
    if (podAddressNode == null) {
      return;
    }
    try {
      KubernetesUtil.formatPodAddress(
          getStringValue(podAddressNode), "yb-pod-0", "yb-pods", "ybdb", "domain.local");
    } catch (RuntimeException e) {
      validationErrors.put(getJsonPath(podAddressNode), e.getMessage());
    }
  }

  private String getStringValue(JsonNode node) {
    return node.get("value").asText();
  }

  private String getJsonPath(JsonNode node) {
    return node.get("jsonPath").asText();
  }

  private JsonNode getKubeConfigNode(Map<String, JsonNode> k8sInfo) {
    // A new or modified kubeconfig is preferred
    // i.e. kubeConfigContent
    return k8sInfo.getOrDefault("kubeConfigContent", k8sInfo.get("kubeConfig"));
  }
}

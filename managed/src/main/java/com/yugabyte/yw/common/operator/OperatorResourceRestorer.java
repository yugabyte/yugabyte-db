// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.HasMetadata;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupSchedule;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.PitrConfig;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.RestoreJob;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.SupportBundle;
import io.yugabyte.operator.v1alpha1.YBCertificate;
import io.yugabyte.operator.v1alpha1.YBProvider;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Restores operator resources from the {@link OperatorResource} table to Kubernetes during HA
 * failover. Each stored resource is applied only when its persisted {@code resourceVersion} is
 * strictly greater than the version currently in Kubernetes, or when the resource does not yet
 * exist in the cluster.
 */
@Singleton
@Slf4j
public class OperatorResourceRestorer {

  private static final Map<String, Class<? extends HasMetadata>> RESOURCE_TYPE_MAP;

  static {
    Map<String, Class<? extends HasMetadata>> map = new HashMap<>();
    map.put("release", Release.class);
    map.put("ybuniverse", YBUniverse.class);
    map.put("storageconfig", StorageConfig.class);
    map.put("backup", Backup.class);
    map.put("drconfig", DrConfig.class);
    map.put("restorejob", RestoreJob.class);
    map.put("support-bundle", SupportBundle.class);
    map.put("ybcertificate", YBCertificate.class);
    map.put("backupschedule", BackupSchedule.class);
    map.put("ybprovider", YBProvider.class);
    map.put("pitrconfig", PitrConfig.class);
    map.put("secret", Secret.class);
    RESOURCE_TYPE_MAP = Collections.unmodifiableMap(map);
  }

  private final RuntimeConfGetter confGetter;
  private final OperatorUtils operatorUtils;
  private final KubernetesClientFactory kubernetesClientFactory;

  @Inject
  public OperatorResourceRestorer(
      RuntimeConfGetter confGetter,
      OperatorUtils operatorUtils,
      KubernetesClientFactory kubernetesClientFactory) {
    this.confGetter = confGetter;
    this.operatorUtils = operatorUtils;
    this.kubernetesClientFactory = kubernetesClientFactory;
  }

  /**
   * Applies all resources from the operator_resource table to Kubernetes when they are newer than
   * the Kubernetes version or absent from the cluster. Intended to be called after HA restore so
   * that the promoted standby's Kubernetes state matches the database.
   */
  public void restoreOperatorResources() {
    if (!confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)) {
      log.info("Kubernetes operator not enabled, skipping operator resource restore");
      return;
    }

    List<OperatorResource> allResources = OperatorResource.getAll();
    if (allResources.isEmpty()) {
      log.info("No operator resources to restore");
      return;
    }

    log.info("Restoring {} operator resources to Kubernetes", allResources.size());
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    io.fabric8.kubernetes.client.Config k8sConfig = operatorUtils.getK8sClientConfig();

    try (KubernetesClient client =
        kubernetesClientFactory.getKubernetesClientWithConfig(k8sConfig)) {
      int applied = 0;
      int skipped = 0;
      int errors = 0;

      for (OperatorResource storedResource : allResources) {
        try {
          if (applyIfNewer(client, storedResource)) {
            applied++;
          } else {
            skipped++;
          }
        } catch (Exception e) {
          errors++;
          log.error("Failed to restore operator resource: {}", storedResource.getName(), e);
        }
      }

      log.info(
          "Operator resource restore complete: applied={}, skipped={}, errors={}",
          applied,
          skipped,
          errors);
    }
  }

  /**
   * Applies a single stored resource to Kubernetes if the stored {@code resourceVersion} is
   * strictly greater than the Kubernetes version, or if the resource does not exist.
   *
   * @return true if the resource was applied, false if skipped
   */
  private boolean applyIfNewer(KubernetesClient client, OperatorResource storedResource) {
    String yamlData = storedResource.getData();
    if (StringUtils.isBlank(yamlData)) {
      log.debug("Skipping resource {} with no data", storedResource.getName());
      return false;
    }

    KubernetesResourceDetails details =
        KubernetesResourceDetails.fromResourceName(storedResource.getName());
    Class<? extends HasMetadata> clazz = RESOURCE_TYPE_MAP.get(details.getResourceType());
    if (clazz == null) {
      log.warn(
          "Unknown resource type '{}', skipping: {}",
          details.getResourceType(),
          storedResource.getName());
      return false;
    }

    HasMetadata resource = Serialization.unmarshal(yamlData, clazz);
    String storedResourceVersion = resource.getMetadata().getResourceVersion();
    String name = resource.getMetadata().getName();
    String ns = resource.getMetadata().getNamespace();

    HasMetadata existing = getExistingResource(client, resource, ns);

    if (existing == null) {
      log.info(
          "Resource {}/{} (type={}) not found in K8s, creating", ns, name, clazz.getSimpleName());
      stripServerManagedMetadata(resource);
      client.resource(resource).inNamespace(ns).create();
      return true;
    }

    String k8sResourceVersion = existing.getMetadata().getResourceVersion();
    if (isStrictlyGreater(storedResourceVersion, k8sResourceVersion)) {
      log.info(
          "Resource {}/{} (type={}) stored version {} > K8s version {}, updating",
          ns,
          name,
          clazz.getSimpleName(),
          storedResourceVersion,
          k8sResourceVersion);
      stripServerManagedMetadata(resource);
      client.resource(resource).inNamespace(ns).patch();
      return true;
    }

    log.debug(
        "Resource {}/{} K8s version {} >= stored version {}, skipping",
        ns,
        name,
        k8sResourceVersion,
        storedResourceVersion);
    return false;
  }

  private HasMetadata getExistingResource(
      KubernetesClient client, HasMetadata resource, String namespace) {
    try {
      return client.resource(resource).inNamespace(namespace).get();
    } catch (Exception e) {
      log.debug(
          "Could not fetch resource {}/{} from K8s, treating as non-existent",
          namespace,
          resource.getMetadata().getName(),
          e);
      return null;
    }
  }

  private static void stripServerManagedMetadata(HasMetadata resource) {
    resource.getMetadata().setUid(null);
    resource.getMetadata().setCreationTimestamp(null);
    resource.getMetadata().setManagedFields(null);
    resource.getMetadata().setGeneration(null);
    resource.getMetadata().setSelfLink(null);
  }

  /**
   * Returns true if storedVersion is strictly greater than k8sVersion when parsed as longs. Falls
   * back to lexicographic comparison if parsing fails.
   */
  static boolean isStrictlyGreater(String storedVersion, String k8sVersion) {
    if (storedVersion == null) return false;
    if (k8sVersion == null) return true;
    try {
      return Long.parseLong(storedVersion) > Long.parseLong(k8sVersion);
    } catch (NumberFormatException e) {
      return storedVersion.compareTo(k8sVersion) > 0;
    }
  }
}

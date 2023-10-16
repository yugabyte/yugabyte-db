// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.ConfigMap;
import io.fabric8.kubernetes.api.model.EventBuilder;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.ObjectReference;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.ConfigBuilder;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.yugabyte.operator.v1alpha1.*;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupStatus;
import io.yugabyte.operator.v1alpha1.RestoreJob;
import io.yugabyte.operator.v1alpha1.RestoreJobStatus;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.directory.api.util.Strings;

@Slf4j
public class KubernetesOperatorStatusUpdater implements OperatorStatusUpdater {

  private final String namespace;
  private final String yugawarePod;
  private final String yugawareNamespace;

  private final Config k8sClientConfig;

  @Inject
  public KubernetesOperatorStatusUpdater(RuntimeConfGetter confGetter) {
    namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    ConfigBuilder confBuilder = new ConfigBuilder();
    if (namespace == null || namespace.trim().isEmpty()) {
      confBuilder.withNamespace(null);
    } else {
      confBuilder.withNamespace(namespace);
    }
    k8sClientConfig = confBuilder.build();

    // Get Yugaware pod and namespace
    this.yugawarePod = System.getProperty("HOSTNAME");
    this.yugawareNamespace = getYugawareNamespace();
  }

  private static String getYugawareNamespace() {
    File file = new File("/var/run/secrets/kubernetes.io/serviceaccount/namespace");
    try {
      BufferedReader br = new BufferedReader(new FileReader(file));
      String ns = br.readLine();
      br.close();
      return ns;
    } catch (Exception e) {
      log.warn("Could not find yugaware pod's namespace");
    }
    return null;
  }

  /*
   * Universe Status Updates
   */
  @Override
  public void createYBUniverseEventStatus(
      Universe universe, KubernetesResourceDetails universeName, String taskName, UUID taskUUID) {
    if (universe.getUniverseDetails().isKubernetesOperatorControlled) {
      try {
        String eventStr =
            String.format(
                "Starting task %s (%s) on universe %s", taskName, taskUUID, universe.getName());
        this.updateUniverseStatus(universe, universeName, eventStr);
      } catch (Exception e) {
        log.warn("Error in creating Kubernetes Operator Universe status", e);
      }
    }
  }

  /*
   * Update Restore Job Status
   */
  @Override
  public void updateRestoreJobStatus(String message, UUID taskUUID) {
    try {
      log.info("Update Restore Job Status called for task {} ", taskUUID);
      try (final KubernetesClient kubernetesClient =
          new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {

        for (RestoreJob restoreJob :
            kubernetesClient.resources(RestoreJob.class).inNamespace(namespace).list().getItems()) {
          if (restoreJob.getStatus().getTaskUUID().equals(taskUUID.toString())) {
            // Found our Restore.
            log.info("Found RestoreJob {} task {} ", restoreJob, taskUUID);
            RestoreJobStatus status = restoreJob.getStatus();

            status.setMessage(message);
            status.setTaskUUID(taskUUID.toString());

            restoreJob.setStatus(status);
            kubernetesClient
                .resources(RestoreJob.class)
                .inNamespace(namespace)
                .resource(restoreJob)
                .replaceStatus();
            log.info("Updated Status for Restore Job CR {}", restoreJob);
            break;
          }
        }
      }
    } catch (Exception e) {
      log.error("Exception in updating restoreJob cr status: {}", e);
    }
  }

  /*
   * Update Backup Status
   */
  @Override
  public void updateBackupStatus(
      com.yugabyte.yw.models.Backup backup, String taskName, UUID taskUUID) {
    try {
      if (backup != null) {
        String universeName = backup.getUniverseName();
        Universe universe = Universe.getUniverseByName(universeName);
        if (universe == null
            || universe.getUniverseDetails() == null
            || !universe.getUniverseDetails().isKubernetesOperatorControlled) {
          log.trace("universe is not operator owned, skipping status update");
          return;
        }
        log.info("Update Backup Status called for task {} ", taskUUID);
        try (final KubernetesClient kubernetesClient =
            new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {

          for (Backup backupCr :
              kubernetesClient.resources(Backup.class).inNamespace(namespace).list().getItems()) {
            if (backupCr.getStatus().getTaskUUID().equals(taskUUID.toString())) {
              // Found our backup.
              log.info("Found Backup {} task {} ", backupCr, taskUUID);
              BackupStatus status = backupCr.getStatus();

              status.setMessage("Backup State: " + backup.getState().name());
              status.setResourceUUID(backup.getBackupUUID().toString());
              status.setTaskUUID(taskUUID.toString());

              backupCr.setStatus(status);
              kubernetesClient
                  .resources(Backup.class)
                  .inNamespace(namespace)
                  .resource(backupCr)
                  .replaceStatus();
              log.info("Updated Status for Backup CR {}", backupCr);
              break;
            }
          }
        }
      }
    } catch (Exception e) {
      // This can happen for a variety of reasons.
      // We might fail to talk to the API server, might need to add retries around this logic
      log.error("Exception in updating backup cr status {}", e);
    }
  }

  @Override
  public void updateYBUniverseStatus(
      Universe universe,
      KubernetesResourceDetails universeName,
      String taskName,
      UUID taskUUID,
      Throwable t) {
    if (universe.getUniverseDetails().isKubernetesOperatorControlled) {
      try {
        // Updating Kubernetes Custom Resource (if done through operator).
        String status = (t != null ? "Failed" : "Succeeded");
        log.info("ybUniverseStatus info: {}: {}", taskName, status);
        String statusStr =
            String.format(
                "Task %s (%s) on universe %s %s", taskName, taskUUID, universe.getName(), status);
        updateUniverseStatus(universe, universeName, statusStr);
      } catch (Exception e) {
        log.warn("Error in creating Kubernetes Operator Universe status", e);
      }
    }
  }

  private void updateUniverseStatus(
      Universe u, KubernetesResourceDetails universeName, String status) {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      YBUniverse ybUniverse = getYBUniverse(kubernetesClient, universeName);
      if (ybUniverse == null) {
        log.info("YBUniverse {}/{} is not found", universeName.namespace, universeName.name);
        return;
      }

      List<String> cqlEndpoints = Arrays.asList(u.getYQLServerAddresses().split(","));
      List<String> sqlEndpoints = Arrays.asList(u.getYSQLServerAddresses().split(","));
      YBUniverseStatus ybUniverseStatus = new YBUniverseStatus();
      ybUniverseStatus.setCqlEndpoints(cqlEndpoints);
      ybUniverseStatus.setSqlEndpoints(sqlEndpoints);
      log.info("Universe status is: {}", status);
      ybUniverse.setStatus(ybUniverseStatus);
      kubernetesClient
          .resources(YBUniverse.class)
          .inNamespace(ybUniverse.getMetadata().getNamespace())
          .resource(ybUniverse)
          .replaceStatus();

      // Update Swamper Targets configMap
      String configMapName = ybUniverse.getMetadata().getName() + "-prometheus-targets";
      // TODO (@anijhawan) should call the swamperHelper target function but we are in static
      // context here.
      String swamperTargetFileName =
          "/opt/yugabyte/prometheus/targets/yugabyte." + u.getUniverseUUID().toString() + ".json";
      String namespace = ybUniverse.getMetadata().getNamespace();
      try {
        updateSwamperTargetConfigMap(configMapName, namespace, swamperTargetFileName);
      } catch (IOException e) {
        log.info("Got Exception in Creating Swamper Targets");
      }
      doKubernetesEventUpdate(universeName, status);
    } catch (Exception e) {
      log.error("Failed to update status: ", e);
    }
  }

  /*
   * SupportBundle Status Updates
   */
  @Override
  public void markSupportBundleFinished(
      com.yugabyte.yw.models.SupportBundle supportBundle,
      KubernetesResourceDetails bundleName,
      Path localPath) {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      SupportBundle bundle = getSupportBundle(kubernetesClient, bundleName);
      if (bundle == null) {
        log.debug(
            "could not find support bundle {} in kubernetes",
            supportBundle.getBundleUUID().toString());
        return;
      }
      SupportBundleStatus bundleStatus = getBundleStatusOrNew(bundle);

      bundleStatus.setStatus(SupportBundleStatus.Status.READY);
      String pod = this.yugawarePod;
      if (this.yugawareNamespace != null) {
        pod = String.format("%s/%s", this.yugawareNamespace, this.yugawarePod);
      }
      if (pod == null) {
        pod = "<yugaware pod name>";
      }
      bundleStatus.setAccess(
          String.format(
              "kubectl cp %s:%s ./%s -c yugaware",
              pod, localPath.toString(), localPath.getFileName().toString()));
      bundle.setStatus(bundleStatus);
      kubernetesClient
          .resources(SupportBundle.class)
          .inNamespace(bundleName.namespace)
          .resource(bundle)
          .replaceStatus();
    } catch (Exception e) {
      log.error("Failed to update status: ", e);
    }
  }

  @Override
  public void markSupportBundleFailed(
      com.yugabyte.yw.models.SupportBundle supportBundle, KubernetesResourceDetails bundleName) {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      SupportBundle bundle = getSupportBundle(kubernetesClient, bundleName);
      if (bundle == null) {
        log.debug("could not find support bundle {} in kubernetes", supportBundle.getBundleUUID());
        return;
      }
      SupportBundleStatus bundleStatus = getBundleStatusOrNew(bundle);
      bundleStatus.setStatus(SupportBundleStatus.Status.FAILED);
      bundle.setStatus(bundleStatus);
      kubernetesClient
          .resources(SupportBundle.class)
          .inNamespace(bundleName.namespace)
          .resource(bundle)
          .replaceStatus();
    } catch (Exception e) {
      log.error("Failed to update status: ", e);
    }
  }

  private SupportBundleStatus getBundleStatusOrNew(SupportBundle bundle) {
    SupportBundleStatus bundleStatus = bundle.getStatus();
    if (bundleStatus == null) {
      bundleStatus = new SupportBundleStatus();
    }
    return bundleStatus;
  }

  private void updateSwamperTargetConfigMap(String configMapName, String namespace, String fileName)
      throws IOException {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      String fileContent = new String(Files.readAllBytes(Paths.get(fileName)));

      // Create a ConfigMap object and populate it with data
      ConfigMap configMap = new ConfigMap();
      configMap.setMetadata(new ObjectMeta());
      configMap.getMetadata().setName(configMapName);
      configMap.getMetadata().setNamespace(namespace);
      configMap
          .getData()
          .put("targets.json", fileContent); // Add the file content as data to the ConfigMap

      // Create or update the ConfigMap in Kubernetes
      ConfigMap createdConfigMap =
          kubernetesClient.configMaps().inNamespace(namespace).createOrReplace(configMap);
      log.info("ConfigMap updated namespace {} configmap {}", namespace, configMapName);
    } catch (Exception e) {
      log.error("Failed to update status: ", e);
    }
  }

  @Override
  public void doKubernetesEventUpdate(KubernetesResourceDetails universeName, String status) {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      YBUniverse ybUniverse = getYBUniverse(kubernetesClient, universeName);
      if (ybUniverse == null) {
        log.error("YBUniverse {} no longer exists", universeName);
        return;
      }
      // Kubernetes Event update
      ObjectReference obj = new ObjectReference();
      obj.setName(ybUniverse.getMetadata().getName());
      obj.setNamespace(ybUniverse.getMetadata().getNamespace());
      String namespace =
          kubernetesClient.getNamespace() != null ? kubernetesClient.getNamespace() : "default";
      kubernetesClient
          .v1()
          .events()
          .inNamespace(namespace)
          .createOrReplace(
              new EventBuilder()
                  .withNewMetadata()
                  .withNamespace(ybUniverse.getMetadata().getNamespace())
                  .withName(ybUniverse.getMetadata().getName())
                  .endMetadata()
                  .withType("Normal")
                  .withReason("Status")
                  .withMessage(status)
                  .withLastTimestamp(DateTimeFormatter.ISO_INSTANT.format(Instant.now()))
                  .withInvolvedObject(obj)
                  .build());
    } catch (Exception e) {
      log.error("Failed to update status: ", e);
    }
  }

  private YBUniverse getYBUniverse(
      KubernetesClient kubernetesClient, KubernetesResourceDetails name) {
    log.debug("lookup ybuniverse {}/{}", name.namespace, name.name);
    return kubernetesClient
        .resources(YBUniverse.class)
        .inNamespace(name.namespace)
        .withName(name.name)
        .get();
  }

  private SupportBundle getSupportBundle(
      KubernetesClient kubernetesClient, KubernetesResourceDetails name) {
    log.debug("lookup support bundle {}/{}", name.namespace, name.name);
    return kubernetesClient
        .resources(SupportBundle.class)
        .inNamespace(name.namespace)
        .withName(name.name)
        .get();
  }

  private Backup getBackupByUUID(KubernetesClient kubernetesClient, UUID uuid) {
    return kubernetesClient
        .resources(Backup.class)
        .inNamespace(namespace)
        .list()
        .getItems()
        .stream()
        .filter(r -> Strings.equals(r.getStatus().getResourceUUID(), uuid.toString()))
        .findFirst()
        .orElse(null);
  }

  private Release getReleaseByUUID(KubernetesClient kubernetesClient, UUID uuid) {
    return kubernetesClient
        .resources(Release.class)
        .inNamespace(namespace)
        .list()
        .getItems()
        .stream()
        .filter(r -> Strings.equals(r.getStatus().getResourceUUID(), uuid.toString()))
        .findFirst()
        .orElse(null);
  }

  private StorageConfig getStorageConfigByUUID(KubernetesClient kubernetesClient, UUID uuid) {
    return kubernetesClient
        .resources(StorageConfig.class)
        .inNamespace(namespace)
        .list()
        .getItems()
        .stream()
        .filter(r -> Strings.equals(r.getStatus().getResourceUUID(), uuid.toString()))
        .findFirst()
        .orElse(null);
  }

  private SupportBundle getSupportBundleByUUID(KubernetesClient kubernetesClient, UUID uuid) {
    return kubernetesClient
        .resources(SupportBundle.class)
        .inNamespace(namespace)
        .list()
        .getItems()
        .stream()
        .filter(r -> Strings.equals(r.getStatus().getResourceUUID(), uuid.toString()))
        .findFirst()
        .orElse(null);
  }

  private YBUniverse getYBUniverseByUUID(KubernetesClient kubernetesClient, UUID uuid) {
    return kubernetesClient
        .resources(YBUniverse.class)
        .inNamespace(namespace)
        .list()
        .getItems()
        .stream()
        .filter(r -> Strings.equals(r.getStatus().getResourceUUID(), uuid.toString()))
        .findFirst()
        .orElse(null);
  }
}

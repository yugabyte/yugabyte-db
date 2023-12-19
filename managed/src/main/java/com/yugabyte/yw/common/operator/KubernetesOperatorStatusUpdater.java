// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.ConfigMap;
import io.fabric8.kubernetes.api.model.Event;
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
import io.yugabyte.operator.v1alpha1.ybuniversestatus.Actions;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
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
        UUID universeUUID = backup.getUniverseUUID();
        Optional<Universe> universeOpt = Universe.maybeGet(universeUUID);
        if (universeOpt.isPresent()
            && universeOpt.get().getUniverseDetails().isKubernetesOperatorControlled) {
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
      }
    } catch (Exception e) {
      // This can happen for a variety of reasons.
      // We might fail to talk to the API server, might need to add retries around this logic
      log.error("Exception in updating backup cr status {}", e);
    }
  }

  /*
   * Universe Status Updates
   */

  // Create a new status action for a universe event. This will add a new action to the ybuniverse
  // resource, starting in a queued state.
  @Override
  public void createYBUniverseEventStatus(
      Universe universe, KubernetesResourceDetails universeName, String taskName) {
    if (universe != null && !universe.getUniverseDetails().isKubernetesOperatorControlled) {
      return;
    }
    try (final KubernetesClient client =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      YBUniverse ybUniverse = getYBUniverse(client, universeName);
      if (ybUniverse == null) {
        log.info("YBUniverse {}/{} is not found", universeName.namespace, universeName.name);
        return;
      }
      YBUniverseStatus ybuStatus = getOrCreateUniverseStatus(ybUniverse);
      Actions action = getOrCreateUniverseAction(ybuStatus, taskName);
      action.setStatus(Actions.Status.QUEUED);
      ybUniverse.setStatus(ybuStatus);
      String eventStr =
          String.format("Queued task %s on universe %s", taskName, universe.getName());
      this.updateUniverseStatus(client, ybUniverse, universe, universeName, eventStr);
    } catch (Exception e) {
      log.warn("Error in creating Kubernetes Operator Universe status", e);
    }
  }

  // Update a universe action to running
  @Override
  public void startYBUniverseEventStatus(
      Universe universe,
      KubernetesResourceDetails universeName,
      String taskName,
      UUID taskUUID,
      UniverseState state,
      boolean isRetry) {
    if (!universe.getUniverseDetails().isKubernetesOperatorControlled) {
      return;
    }
    try (final KubernetesClient client =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      YBUniverse ybUniverse = getYBUniverse(client, universeName);
      if (ybUniverse == null) {
        log.info("YBUniverse {}/{} is not found", universeName.namespace, universeName.name);
        return;
      }
      YBUniverseStatus ybuStatus = getOrCreateUniverseStatus(ybUniverse);
      Actions action = getOrCreateUniverseAction(ybuStatus, taskName);
      action.setStatus(Actions.Status.RUNNING);
      String actionStr = isRetry ? "Retrying" : "Starting";
      String eventStr =
          String.format(
              "%s Task %s (%s) on universe %s", actionStr, taskName, taskUUID, universe.getName());
      action.setMessage(eventStr);
      // TODO: Check if this is universe create and set to provisioning.
      ybuStatus.setUniverseState(state.getUniverseStateString());
      ybUniverse.setStatus(ybuStatus);
      this.updateUniverseStatus(client, ybUniverse, universe, universeName, eventStr);
    } catch (Exception e) {
      log.warn("Error in creating Kubernetes Operator Universe status", e);
    }
  }

  // After a task completes (successfully or otherwise), update the status so it is reflected in
  // the CR.
  // on success, we will remove the related action from the actions list.
  // Failure will update the action with 'failed'
  // Both will create a kubernetes event.
  @Override
  public void updateYBUniverseStatus(
      Universe universe,
      KubernetesResourceDetails universeName,
      String taskName,
      UUID taskUUID,
      UniverseState state,
      Throwable t) {
    if (!universe.getUniverseDetails().isKubernetesOperatorControlled) {
      return;
    }
    try (final KubernetesClient client =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      YBUniverse ybUniverse = getYBUniverse(client, universeName);
      if (ybUniverse == null) {
        log.info("YBUniverse {}/{} is not found", universeName.namespace, universeName.name);
        return;
      }
      YBUniverseStatus status = ybUniverse.getStatus();
      status.setUniverseState(state.getUniverseStateString());
      // Handle the success case
      String message = null;
      if (t == null) {
        removeUniverseAction(status, taskName);
        message = String.format("Task %s (%s) succeeded", taskName, taskUUID);
      } else {
        Actions action = getOrCreateUniverseAction(status, taskName);
        action.setStatus(Actions.Status.FAILED);
        message = String.format("Task %s(%s) failed: %s", taskName, taskUUID, t.getMessage());
        action.setMessage(message);
      }
      // Updating Kubernetes Custom Resource (if done through operator).
      this.updateUniverseStatus(client, ybUniverse, universe, universeName, message);
    } catch (Exception e) {
      log.warn("Error in creating Kubernetes Operator Universe status", e);
    }
  }

  private void updateUniverseStatus(
      KubernetesClient kubernetesClient,
      YBUniverse ybUniverse,
      Universe u,
      KubernetesResourceDetails universeName,
      String eventMsg) {
    try {
      // TODO: We should be able to only update these when needed.
      if (u != null) {
        List<String> cqlEndpoints = Arrays.asList(u.getYQLServerAddresses().split(","));
        List<String> sqlEndpoints = Arrays.asList(u.getYSQLServerAddresses().split(","));
        YBUniverseStatus ybUniverseStatus = getOrCreateUniverseStatus(ybUniverse);
        ybUniverseStatus.setCqlEndpoints(cqlEndpoints);
        ybUniverseStatus.setSqlEndpoints(sqlEndpoints);
        ybUniverse.setStatus(ybUniverseStatus);
      }
      // Update the universe CR status.
      kubernetesClient
          .resources(YBUniverse.class)
          .inNamespace(ybUniverse.getMetadata().getNamespace())
          .resource(ybUniverse)
          .updateStatus(); // Note: Vscode is saying this is invalid, but it is the right way.

      // Update Swamper Targets configMap
      String configMapName = ybUniverse.getMetadata().getName() + "-prometheus-targets";
      // TODO (@anijhawan) should call the swamperHelper target function but we are in static
      // context here.
      if (u != null) {
        String swamperTargetFileName =
            "/opt/yugabyte/prometheus/targets/yugabyte." + u.getUniverseUUID().toString() + ".json";
        String namespace = ybUniverse.getMetadata().getNamespace();
        try {
          updateSwamperTargetConfigMap(configMapName, namespace, swamperTargetFileName);
        } catch (IOException e) {
          log.warn("Got Exception in Creating Swamper Targets");
        }
      }
      doKubernetesEventUpdate(universeName, eventMsg);
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
      log.warn("failed to update config map with swamper targets: ", e);
    }
  }

  @Override
  public void doKubernetesEventUpdate(KubernetesResourceDetails universeName, String eventMsg) {
    try (final KubernetesClient kubernetesClient =
        new KubernetesClientBuilder().withConfig(k8sClientConfig).build()) {
      // Kubernetes Event update
      ObjectReference obj = new ObjectReference();
      obj.setName(universeName.name);
      obj.setNamespace(universeName.namespace);
      obj.setKind("YBUniverse");

      String namespace =
          kubernetesClient.getNamespace() != null ? kubernetesClient.getNamespace() : "default";
      Event event =
          new EventBuilder()
              .withNewMetadata()
              .withNamespace(universeName.namespace)
              .withName(universeName.name)
              .endMetadata()
              .withType("Normal")
              .withReason("Status")
              .withMessage(eventMsg)
              .withLastTimestamp(DateTimeFormatter.ISO_INSTANT.format(Instant.now()))
              .withInvolvedObject(obj)
              .build();
      kubernetesClient.v1().events().inNamespace(namespace).resource(event).createOrReplace();
    } catch (Exception e) {
      log.warn("Failed to create kubernetes event: ", e);
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

  private YBUniverseStatus getOrCreateUniverseStatus(YBUniverse universe) {
    YBUniverseStatus status = universe.getStatus();
    if (status == null) {
      log.debug("Creating new universe status for %s", universe.getMetadata().getName());
      status = new YBUniverseStatus();
    }
    return status;
  }

  // getOrCreateUniverseAction will look through all the actions in the ybUniverse status for the
  // action related to the task type. If found, it will return that specific action in the list.
  // Otherwise, a new action will be created, set to that taskType, appended to the actions list,
  // and returned.
  private synchronized Actions getOrCreateUniverseAction(YBUniverseStatus status, String taskType) {
    List<Actions> actionsList = status.getActions();
    if (actionsList == null) {
      actionsList = new ArrayList<Actions>();
    }
    Iterator<Actions> it = actionsList.iterator();
    Actions action = null;
    while (it.hasNext()) {
      action = it.next();
      if (action.getAction_type().equals(taskType)) {
        log.debug("found action for type {}", taskType);
        return action;
      }
    }
    log.debug("creating action for type {}", taskType);
    action = new Actions();
    action.setAction_type(taskType);
    actionsList.add(action);
    status.setActions(actionsList);
    return action;
  }

  private synchronized void removeUniverseAction(YBUniverseStatus status, String taskType) {
    List<Actions> actionsList = status.getActions();
    for (int i = 0; i < actionsList.size(); i++) {
      if (actionsList.get(i).getAction_type().equals(taskType)) {
        log.debug("remove action for type {}", taskType);
        actionsList.remove(i);
        return;
      }
    }
    log.debug("No tasks for type {} found", taskType);
  }
}

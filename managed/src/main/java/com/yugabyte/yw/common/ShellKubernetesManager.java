// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.JsonParser.Feature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.NodeList;
import io.fabric8.kubernetes.api.model.PersistentVolumeClaim;
import io.fabric8.kubernetes.api.model.PersistentVolumeClaimCondition;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.ServiceList;
import io.fabric8.kubernetes.api.model.events.v1.Event;
import io.fabric8.kubernetes.api.model.events.v1.EventList;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yaml.snakeyaml.Yaml;

@Singleton
@Slf4j
public class ShellKubernetesManager extends KubernetesManager {

  @Inject ReleaseManager releaseManager;

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject play.Configuration appConfig;

  public static final Logger LOG = LoggerFactory.getLogger(ShellKubernetesManager.class);

  private ShellResponse execCommand(Map<String, String> config, List<String> command) {
    return execCommand(config, command, true /*logCmdOutput*/);
  }

  private ShellResponse execCommand(
      Map<String, String> config, List<String> command, boolean logCmdOutput) {
    String description = String.join(" ", command);
    return shellProcessHandler.run(
        command,
        ShellProcessContext.builder()
            .description(description)
            .extraEnvVars(config)
            .logCmdOutput(logCmdOutput)
            .build());
  }

  private <T> T deserialize(String json, Class<T> type) {
    try {
      return new ObjectMapper().configure(Feature.ALLOW_SINGLE_QUOTES, true).readValue(json, type);
    } catch (Exception e) {
      throw new RuntimeException("Error deserializing response from kubectl command: ", e);
    }
  }

  @Override
  public void createNamespace(Map<String, String> config, String namespace) {
    String namespaceFile = generateNamespaceYaml(namespace);
    List<String> commandList =
        ImmutableList.of("kubectl", "apply", "-f", namespaceFile, "--server-side");
    execCommand(config, commandList).processErrors();
  }

  // TODO(bhavin192): modify the pullSecret on the fly while applying
  // it? Add nodePrefix to the name, add labels which make it easy to
  // find the secret by label selector, and even delete it if
  // required. Something like, -lprovider=gke1 or
  // -luniverse=uni1. Tracked here:
  // https://github.com/yugabyte/yugabyte-db/issues/7012
  @Override
  public void applySecret(Map<String, String> config, String namespace, String pullSecret) {
    // --server-side flag ensures that there is no race condition when
    // multiple kubectl apply try to apply same secret in same
    // namespace.
    List<String> commandList =
        ImmutableList.of(
            "kubectl", "apply", "-f", pullSecret, "--namespace", namespace, "--server-side");
    execCommand(config, commandList).processErrors();
  }

  @Override
  public List<Pod> getPodInfos(
      Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "get",
            "pods",
            "--namespace",
            namespace,
            "-o",
            "json",
            "-l",
            "release=" + helmReleaseName);
    ShellResponse response =
        execCommand(config, commandList, false /*logCmdOutput*/).processErrors();
    return deserialize(response.message, PodList.class).getItems();
  }

  @Override
  public List<Service> getServices(
      Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "get",
            "services",
            "--namespace",
            namespace,
            "-o",
            "json",
            "-l",
            "release=" + helmReleaseName);
    ShellResponse response = execCommand(config, commandList).processErrors();
    return deserialize(response.message, ServiceList.class).getItems();
  }

  @Override
  public PodStatus getPodStatus(Map<String, String> config, String namespace, String podName) {
    List<String> commandList =
        ImmutableList.of("kubectl", "get", "pod", "--namespace", namespace, "-o", "json", podName);
    ShellResponse response =
        execCommand(config, commandList, false /*logCmdOutput*/).processErrors();
    return deserialize(response.message, Pod.class).getStatus();
  }

  @Override
  public String getPreferredServiceIP(
      Map<String, String> config,
      String universePrefix,
      String namespace,
      boolean isMaster,
      boolean newNamingStyle) {
    String appLabel = newNamingStyle ? "app.kubernetes.io/name" : "app";
    String appName = isMaster ? "yb-master" : "yb-tserver";
    // TODO(bhavin192): this might need to be changed when we support
    // multi-cluster environments.
    String selector =
        String.format("release=%s,%s=%s,service-type!=headless", universePrefix, appLabel, appName);
    List<String> commandList =
        ImmutableList.of(
            "kubectl", "get", "svc", "--namespace", namespace, "-l", selector, "-o", "json");
    ShellResponse response = execCommand(config, commandList).processErrors();
    List<Service> services = deserialize(response.message, ServiceList.class).getItems();
    if (services.size() != 1) {
      throw new RuntimeException(
          "There must be exactly one Master or TServer endpoint service, got " + services.size());
    }
    return getIp(services.get(0));
  }

  @Override
  public List<Node> getNodeInfos(Map<String, String> config) {
    List<String> commandList = ImmutableList.of("kubectl", "get", "nodes", "-o", "json");
    ShellResponse response =
        execCommand(config, commandList, false /*logCmdOutput*/)
            .processErrors("Unable to get node information");
    return deserialize(response.message, NodeList.class).getItems();
  }

  @Override
  public Secret getSecret(Map<String, String> config, String secretName, String namespace) {
    List<String> commandList = new ArrayList<String>();
    commandList.addAll(ImmutableList.of("kubectl", "get", "secret", secretName, "-o", "json"));
    if (namespace != null) {
      commandList.add("--namespace");
      commandList.add(namespace);
    }
    ShellResponse response =
        execCommand(config, commandList, false /*logCmdOutput*/)
            .processErrors("Unable to get secret");
    return deserialize(response.message, Secret.class);
  }

  @Override
  public void updateNumNodes(
      Map<String, String> config,
      String universePrefix,
      String namespace,
      int numNodes,
      boolean newNamingStyle) {
    String appLabel = newNamingStyle ? "app.kubernetes.io/name" : "app";
    String selector = String.format("release=%s,%s=yb-tserver", universePrefix, appLabel);
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "--namespace",
            namespace,
            "scale",
            "statefulset",
            "-l",
            selector,
            "--replicas=" + numNodes);
    execCommand(config, commandList).processErrors();
  }

  @Override
  public void deleteStorage(Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    // Delete Master and TServer Volumes
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "delete",
            "pvc",
            "--namespace",
            namespace,
            "-l",
            "release=" + helmReleaseName);
    execCommand(config, commandList);
    // TODO: check the execCommand outputs.
  }

  @Override
  public void deleteNamespace(Map<String, String> config, String namespace) {
    // Delete Namespace
    List<String> masterCommandList = ImmutableList.of("kubectl", "delete", "namespace", namespace);
    execCommand(config, masterCommandList);
    // TODO: process any errors. Don't raise exception in case of not
    // found error, this can happen when same namespace is deleted by
    // multiple invocations concurrently.
  }

  @Override
  public void deletePod(Map<String, String> config, String namespace, String podName) {
    List<String> masterCommandList =
        ImmutableList.of("kubectl", "--namespace", namespace, "delete", "pod", podName);
    execCommand(config, masterCommandList).processErrors("Unable to delete pod");
  }

  @Override
  public List<Event> getEvents(Map<String, String> config, String namespace) {
    List<String> commandList =
        ImmutableList.of("kubectl", "get", "events", "-n", namespace, "-o", "json");
    ShellResponse response =
        execCommand(config, commandList, false /*logCmdOutput*/)
            .processErrors("Unable to list events");
    return deserialize(response.message, EventList.class).getItems();
  }

  // generateNamespaceYaml creates a namespace YAML file for given
  // name. This can be later extended to add other metadata like
  // labels, annotations etc.
  private String generateNamespaceYaml(String name) {
    Map<String, Object> namespace = new HashMap<String, Object>();
    namespace.put("apiVersion", "v1");
    namespace.put("kind", "Namespace");
    namespace.put("metadata", ImmutableMap.of("name", name));
    Yaml yaml = new Yaml();

    try {
      Path tempFile = Files.createTempFile(UUID.randomUUID().toString() + "-namespace", ".yml");
      BufferedWriter bw = new BufferedWriter(new FileWriter(tempFile.toFile()));
      yaml.dump(namespace, bw);
      return tempFile.toAbsolutePath().toString();
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Error writing Namespace YAML file.");
    }
  }

  @Override
  public boolean deleteStatefulSet(Map<String, String> config, String namespace, String stsName) {
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "--namespace",
            namespace,
            "delete",
            "statefulset",
            stsName,
            "--cascade=orphan");
    ShellResponse response =
        execCommand(config, commandList, false).processErrors("Unable to delete StatefulSet");
    return response.isSuccess();
  }

  @Override
  public boolean expandPVC(
      Map<String, String> config,
      String namespace,
      String universePrefix,
      String appLabel,
      String newDiskSize) {
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    String labelSelector = String.format("app=%s,release=%s", appLabel, helmReleaseName);
    List<String> commandList =
        ImmutableList.of(
            "kubectl", "--namespace", namespace, "get", "pvc", "-l", labelSelector, "-o", "name");
    ShellResponse response =
        execCommand(config, commandList, false).processErrors("Unable to get PVCs");
    log.info("Expanding PVCs: {}", response.getMessage());
    ObjectNode patchObj = Json.newObject();
    patchObj
        .putObject("spec")
        .putObject("resources")
        .putObject("requests")
        .put("storage", newDiskSize);
    String patchStr = patchObj.toString();
    boolean patchSuccess = true;
    for (String pvcName : response.getMessage().split("\n")) {
      commandList =
          ImmutableList.of("kubectl", "--namespace", namespace, "patch", pvcName, "-p", patchStr);
      response = execCommand(config, commandList, false).processErrors("Unable to patch PVC");
      patchSuccess &= response.isSuccess() && waitForPVCExpand(config, namespace, pvcName);
    }
    return patchSuccess;
  }

  // Ref: https://kubernetes.io/blog/2022/05/05/volume-expansion-ga/
  // The PVC status condition is cleared once PVC resize is done.
  private boolean waitForPVCExpand(Map<String, String> config, String namespace, String pvcName) {
    RetryTaskUntilCondition<List<PersistentVolumeClaimCondition>> waitForExpand =
        new RetryTaskUntilCondition<>(
            // task
            () -> {
              List<String> commandList =
                  ImmutableList.of(
                      "kubectl", "--namespace", namespace, "get", pvcName, "-o", "json");
              ShellResponse response =
                  execCommand(config, commandList, false).processErrors("Unable to get PVC");
              List<PersistentVolumeClaimCondition> pvcConditions =
                  deserialize(response.message, PersistentVolumeClaim.class)
                      .getStatus()
                      .getConditions();
              return pvcConditions;
            },
            // until condition
            pvcConditions -> {
              if (!pvcConditions.isEmpty()) {
                log.info(
                    "Waiting for condition to clear from PVC {}/{}: {}",
                    namespace,
                    pvcName,
                    pvcConditions.get(0));
              }
              return pvcConditions.isEmpty();
            },
            // delay between retry of task
            2,
            // timeout for retry in secs
            getTimeoutSecs());
    return waitForExpand.retryUntilCond();
  }

  @Override
  public String getStorageClassName(
      Map<String, String> config, String namespace, String universePrefix, boolean forMaster) {
    String appLabel = forMaster ? "yb-master" : "yb-tserver";
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    String labelSelector = String.format("app=%s,release=%s", appLabel, helmReleaseName);
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "--namespace",
            namespace,
            "get",
            "pvc",
            "-l",
            labelSelector,
            "-o",
            "jsonpath='{.items[0].spec.storageClassName}'");
    ShellResponse response =
        execCommand(config, commandList, false)
            .processErrors("Unable to read StorageClass name for yb-tserver PVC");
    return deserialize(response.getMessage(), String.class);
  }

  @Override
  public boolean storageClassAllowsExpansion(Map<String, String> config, String storageClassName) {
    List<String> commandList =
        ImmutableList.of(
            "kubectl", "get", "sc", storageClassName, "-o", "jsonpath='{.allowVolumeExpansion}'");
    ShellResponse response =
        execCommand(config, commandList, false)
            .processErrors("Unable to read StorageClass volume expansion");
    String allowsExpansion = deserialize(response.getMessage(), String.class);
    return allowsExpansion.equalsIgnoreCase("true");
  }
}

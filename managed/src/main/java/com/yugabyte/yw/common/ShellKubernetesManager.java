// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.NodeList;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.ServiceList;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CancellationException;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class ShellKubernetesManager extends KubernetesManager {

  @Inject ReleaseManager releaseManager;

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject play.Configuration appConfig;

  public static final Logger LOG = LoggerFactory.getLogger(ShellKubernetesManager.class);

  private ShellResponse execCommand(Map<String, String> config, List<String> command) {
    String description = String.join(" ", command);
    return shellProcessHandler.run(command, config, description);
  }

  public void processShellResponse(ShellResponse response) {
    if (response.code == ShellResponse.ERROR_CODE_EXECUTION_CANCELLED) {
      throw new CancellationException((response.message != null) ? response.message : "error");
    }
    if (response.code != ShellResponse.ERROR_CODE_SUCCESS) {
      throw new RuntimeException((response.message != null) ? response.message : "error");
    }
  }

  private <T> T deserialize(String json, Class<T> type) {
    try {
      return new ObjectMapper().readValue(json, type);
    } catch (Exception e) {
      throw new RuntimeException("Error deserializing response from kubectl command: ", e);
    }
  }

  @Override
  public void createNamespace(Map<String, String> config, String namespace) {
    List<String> commandList = ImmutableList.of("kubectl", "create", "namespace", namespace);
    processShellResponse(execCommand(config, commandList));
  }

  // TODO(bhavin192): modify the pullSecret on the fly while applying
  // it? Add nodePrefix to the name, add labels which make it easy to
  // find the secret by label selector, and even delete it if
  // required. Something like, -lprovider=gke1 or
  // -luniverse=uni1. Tracked here:
  // https://github.com/yugabyte/yugabyte-db/issues/7012
  @Override
  public void applySecret(Map<String, String> config, String namespace, String pullSecret) {
    List<String> commandList =
        ImmutableList.of("kubectl", "apply", "-f", pullSecret, "--namespace", namespace);
    processShellResponse(execCommand(config, commandList));
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
    String response = execCommand(config, commandList).message;
    return deserialize(response, PodList.class).getItems();
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
    String response = execCommand(config, commandList).message;
    return deserialize(response, ServiceList.class).getItems();
  }

  @Override
  public PodStatus getPodStatus(Map<String, String> config, String namespace, String podName) {
    List<String> commandList =
        ImmutableList.of("kubectl", "get", "pod", "--namespace", namespace, "-o", "json", podName);
    String response = execCommand(config, commandList).message;
    return deserialize(response, Pod.class).getStatus();
  }

  @Override
  public String getPreferredServiceIP(
      Map<String, String> config, String namespace, boolean isMaster) {
    String serviceName = isMaster ? "yb-master-service" : "yb-tserver-service";
    List<String> commandList =
        ImmutableList.of(
            "kubectl", "get", "svc", serviceName, "--namespace", namespace, "-o", "json");
    String response = execCommand(config, commandList).message;
    Service service = deserialize(response, Service.class);
    return getIp(service);
  }

  @Override
  public List<Node> getNodeInfos(Map<String, String> config) {
    List<String> commandList = ImmutableList.of("kubectl", "get", "nodes", "-o", "json");
    ShellResponse response = execCommand(config, commandList);
    if (response.code != 0) {
      String msg = "Unable to get node information";
      if (!response.message.isEmpty()) {
        msg = String.format("%s: %s", msg, response.message);
      }
      throw new RuntimeException(msg);
    }
    return deserialize(response.message, NodeList.class).getItems();
  }

  // TODO: disable the logging of stdout of this command if possibile,
  // as it just leaks the secret content in the logs at DEBUG level.
  @Override
  public Secret getSecret(Map<String, String> config, String secretName, String namespace) {
    List<String> commandList = new ArrayList<String>();
    commandList.addAll(ImmutableList.of("kubectl", "get", "secret", secretName, "-o", "json"));
    if (namespace != null) {
      commandList.add("--namespace");
      commandList.add(namespace);
    }
    ShellResponse response = execCommand(config, commandList);
    if (response.code != 0) {
      String msg = "Unable to get secret";
      if (!response.message.isEmpty()) {
        msg = String.format("%s: %s", msg, response.message);
      }
      throw new RuntimeException(msg);
    }
    return deserialize(response.message, Secret.class);
  }

  @Override
  public void updateNumNodes(Map<String, String> config, String namespace, int numNodes) {
    List<String> commandList =
        ImmutableList.of(
            "kubectl",
            "--namespace",
            namespace,
            "scale",
            "statefulset",
            "yb-tserver",
            "--replicas=" + numNodes);
    processShellResponse(execCommand(config, commandList));
  }

  @Override
  public void deleteStorage(Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    // Delete Master Volumes
    List<String> masterCommandList =
        ImmutableList.of(
            "kubectl",
            "delete",
            "pvc",
            "--namespace",
            namespace,
            "-l",
            "app=yb-master,release=" + helmReleaseName);
    execCommand(config, masterCommandList);
    // Delete TServer Volumes
    List<String> tserverCommandList =
        ImmutableList.of(
            "kubectl",
            "delete",
            "pvc",
            "--namespace",
            namespace,
            "-l",
            "app=yb-tserver,release=" + helmReleaseName);
    execCommand(config, tserverCommandList);
    // TODO: check the execCommand outputs.
  }

  @Override
  public void deleteNamespace(Map<String, String> config, String namespace) {
    // Delete Namespace
    List<String> masterCommandList = ImmutableList.of("kubectl", "delete", "namespace", namespace);
    execCommand(config, masterCommandList);
  }
}

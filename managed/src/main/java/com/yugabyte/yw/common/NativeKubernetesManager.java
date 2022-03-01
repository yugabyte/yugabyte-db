package com.yugabyte.yw.common;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;
import java.util.Map;

import javax.annotation.Nullable;
import javax.inject.Singleton;

import io.fabric8.kubernetes.api.model.NamespaceBuilder;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.DefaultKubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClient;

@Singleton
public class NativeKubernetesManager extends KubernetesManager {
  private KubernetesClient getClient(Map<String, String> config) {
    if (config.containsKey("KUBECONFIG")) {
      try {
        String kubeConfigContents =
            new String(Files.readAllBytes(Paths.get(config.get("KUBECONFIG"))));
        Config kubernetesConfig = Config.fromKubeconfig(kubeConfigContents);
        return new DefaultKubernetesClient(kubernetesConfig);
      } catch (IOException e) {
        throw new RuntimeException("Unable to resolve Kubernetes Client: ", e);
      }
    }
    return new DefaultKubernetesClient();
  }

  @Override
  public void createNamespace(Map<String, String> config, String universePrefix) {
    try (KubernetesClient client = getClient(config)) {
      client
          .namespaces()
          .create(
              new NamespaceBuilder()
                  .withNewMetadata()
                  .withName(universePrefix)
                  .endMetadata()
                  .build());
    }
  }

  @Override
  public void applySecret(Map<String, String> config, String namespace, String pullSecret) {
    try (KubernetesClient client = getClient(config)) {
      client
          .load(NativeKubernetesManager.class.getResourceAsStream(pullSecret))
          .inNamespace(namespace)
          .createOrReplace();
    }
  }

  @Override
  public List<Pod> getPodInfos(
      Map<String, String> config, String universePrefix, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      return client
          .pods()
          .inNamespace(namespace)
          .withLabel("release", universePrefix)
          .list()
          .getItems();
    }
  }

  @Override
  public List<Service> getServices(
      Map<String, String> config, String universePrefix, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      return client
          .services()
          .inNamespace(namespace)
          .withLabel("release", universePrefix)
          .list()
          .getItems();
    }
  }

  @Override
  public PodStatus getPodStatus(Map<String, String> config, String namespace, String podName) {
    try (KubernetesClient client = getClient(config)) {
      return client.pods().inNamespace(namespace).withName(podName).get().getStatus();
    }
  }

  @Override
  public String getPreferredServiceIP(
      Map<String, String> config, String namespace, boolean isMaster) {
    String serviceName = isMaster ? "yb-master-service" : "yb-tserver-service";
    try (KubernetesClient client = getClient(config)) {
      Service service = client.services().inNamespace(namespace).withName(serviceName).get();
      return getIp(service);
    }
  }

  @Override
  public List<Node> getNodeInfos(Map<String, String> config) {
    try (KubernetesClient client = getClient(config)) {
      return client.nodes().list().getItems();
    }
  }

  @Override
  public Secret getSecret(
      Map<String, String> config, String secretName, @Nullable String namespace) {
    try (KubernetesClient client = getClient(config)) {
      if (namespace == null) {
        return client.secrets().withName(secretName).get();
      }
      return client.secrets().inNamespace(namespace).withName(secretName).get();
    }
  }

  @Override
  public void updateNumNodes(Map<String, String> config, String namespace, int numNodes) {
    try (KubernetesClient client = getClient(config)) {
      client.apps().statefulSets().inNamespace(namespace).withName("yb-tserver").scale(numNodes);
    }
  }

  @Override
  public void deleteStorage(Map<String, String> config, String universePrefix, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      client
          .persistentVolumeClaims()
          .inNamespace(namespace)
          .withLabel("app", "yb-master")
          .withLabel("release", universePrefix)
          .delete();
      client
          .persistentVolumeClaims()
          .inNamespace(namespace)
          .withLabel("app", "yb-tserver")
          .withLabel("release", universePrefix)
          .delete();
    }
  }

  @Override
  public void deleteNamespace(Map<String, String> config, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      client.namespaces().withName(namespace).delete();
    }
  }
}

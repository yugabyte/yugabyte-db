// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import io.fabric8.kubernetes.api.model.NamespaceBuilder;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.apps.StatefulSet;
import io.fabric8.kubernetes.api.model.apps.StatefulSetList;
import io.fabric8.kubernetes.api.model.events.v1.Event;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.DefaultKubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.RollableScalableResource;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Map;
import javax.annotation.Nullable;
import javax.inject.Singleton;

@Singleton
public class NativeKubernetesManager extends KubernetesManager {
  private KubernetesClient getClient(Map<String, String> config) {
    if (config.containsKey("KUBECONFIG") && !config.get("KUBECONFIG").isEmpty()) {
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
  public void createNamespace(Map<String, String> config, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      client
          .namespaces()
          .createOrReplace(
              new NamespaceBuilder().withNewMetadata().withName(namespace).endMetadata().build());
    }
  }

  @Override
  public void applySecret(Map<String, String> config, String namespace, String pullSecret) {
    try (KubernetesClient client = getClient(config);
        InputStream pullSecretStream =
            Files.newInputStream(Paths.get(pullSecret), StandardOpenOption.READ); ) {
      client.load(pullSecretStream).inNamespace(namespace).createOrReplace();
    } catch (IOException e) {
      throw new RuntimeException("Unable to get the pullSecret ", e);
    }
  }

  @Override
  public List<Pod> getPodInfos(
      Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    try (KubernetesClient client = getClient(config)) {
      return client
          .pods()
          .inNamespace(namespace)
          .withLabel("release", helmReleaseName)
          .list()
          .getItems();
    }
  }

  @Override
  public List<Service> getServices(
      Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    try (KubernetesClient client = getClient(config)) {
      return client
          .services()
          .inNamespace(namespace)
          .withLabel("release", helmReleaseName)
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
      Map<String, String> config,
      String universePrefix,
      String namespace,
      boolean isMaster,
      boolean newNamingStyle) {
    String appLabel = newNamingStyle ? "app.kubernetes.io/name" : "app";
    String appName = isMaster ? "yb-master" : "yb-tserver";
    try (KubernetesClient client = getClient(config)) {
      // TODO(bhavin192): this might need to be changed when we
      // support multi-cluster environments.
      List<Service> services =
          client
              .services()
              .inNamespace(namespace)
              .withLabel(appLabel, appName)
              .withLabel("release", universePrefix)
              .withoutLabel("service-type", "headless")
              .list()
              .getItems();
      if (services.size() != 1) {
        throw new RuntimeException(
            "There must be exactly one Master or TServer endpoint service, got " + services.size());
      }
      return getIp(services.get(0));
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
  public void updateNumNodes(
      Map<String, String> config,
      String universePrefix,
      String namespace,
      int numNodes,
      boolean newNamingStyle) {
    String appLabel = newNamingStyle ? "app.kubernetes.io/name" : "app";
    try (KubernetesClient client = getClient(config)) {
      // https://github.com/fabric8io/kubernetes-client/issues/3948
      MixedOperation<StatefulSet, StatefulSetList, RollableScalableResource<StatefulSet>>
          statefulSets = client.apps().statefulSets();
      statefulSets
          .inNamespace(namespace)
          .withLabel("release", universePrefix)
          .withLabel(appLabel, "yb-tserver")
          .list()
          .getItems()
          .forEach(
              s ->
                  statefulSets
                      .inNamespace(namespace)
                      .withName(s.getMetadata().getName())
                      .scale(numNodes));
    }
  }

  @Override
  public void deleteStorage(Map<String, String> config, String universePrefix, String namespace) {
    // Implementation specific helm release name.
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    try (KubernetesClient client = getClient(config)) {
      client
          .persistentVolumeClaims()
          .inNamespace(namespace)
          .withLabel("release", helmReleaseName)
          .delete();
    }
  }

  @Override
  public void deleteNamespace(Map<String, String> config, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      client.namespaces().withName(namespace).delete();
    }
  }

  @Override
  public void deletePod(Map<String, String> config, String namespace, String podName) {
    try (KubernetesClient client = getClient(config)) {
      client.pods().inNamespace(namespace).withName(podName).delete();
    }
  }

  @Override
  public List<Event> getEvents(Map<String, String> config, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      return client.events().v1().events().inNamespace(namespace).list().getItems();
    }
  }
}

// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import io.fabric8.kubernetes.api.model.DeletionPropagation;
import io.fabric8.kubernetes.api.model.Namespace;
import io.fabric8.kubernetes.api.model.NamespaceBuilder;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.NodeSpec;
import io.fabric8.kubernetes.api.model.PersistentVolumeClaim;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Quantity;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.apps.StatefulSet;
import io.fabric8.kubernetes.api.model.apps.StatefulSetList;
import io.fabric8.kubernetes.api.model.events.v1.Event;
import io.fabric8.kubernetes.api.model.storage.StorageClass;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.DefaultKubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.ExecListener;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.RollableScalableResource;
import io.fabric8.kubernetes.client.dsl.base.PatchContext;
import io.fabric8.kubernetes.client.dsl.base.PatchType;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import okhttp3.Response;

@Singleton
@Slf4j
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

  static class SimpleListener implements ExecListener {

    private CompletableFuture<String> data;
    private ByteArrayOutputStream baos;

    public SimpleListener(CompletableFuture<String> data, ByteArrayOutputStream baos) {
      this.data = data;
      this.baos = baos;
    }

    // @Override
    // public void onOpen(Response response) {
    //   log.info("Reading data... ");
    // }

    @Override
    public void onFailure(Throwable t, Response failureResponse) {
      log.error(t.getMessage());
      data.completeExceptionally(t);
    }

    @Override
    public void onClose(int code, String reason) {
      log.info("Exit with: " + code + " and with reason: " + reason);
      data.complete(baos.toString());
    }
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
      Map<String, String> config, String helmReleaseName, String namespace) {
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
      Map<String, String> config, String helmReleaseName, String namespace) {
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
  public List<Namespace> getNamespaces(Map<String, String> config) {
    try (KubernetesClient client = getClient(config)) {
      return client.namespaces().list().getItems();
    }
  }

  @Override
  public boolean dbNamespaceExists(Map<String, String> config, String namespace) {
    throw new UnsupportedOperationException("Unimplemented method 'dbNamespaceExists'");
  }

  @Override
  public PodStatus getPodStatus(Map<String, String> config, String namespace, String podName) {
    try (KubernetesClient client = getClient(config)) {
      return client.pods().inNamespace(namespace).withName(podName).get().getStatus();
    }
  }

  @Override
  public Pod getPodObject(Map<String, String> config, String namespace, String podName) {
    try (KubernetesClient client = getClient(config)) {
      return client.pods().inNamespace(namespace).withName(podName).get();
    }
  }

  @Override
  public String getCloudProvider(Map<String, String> config) {
    try (KubernetesClient client = getClient(config)) {
      Node node = client.nodes().list().getItems().get(0);
      NodeSpec spec = node.getSpec();
      String provider = spec.getProviderID().split(":")[0];
      return provider;
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
      // We don't use service-type=endpoint selector for backwards
      // compatibility with old charts which don't have service-type
      // label on endpoint/exposed services.
      List<Service> services =
          client
              .services()
              .inNamespace(namespace)
              .withLabel(appLabel, appName)
              .withLabel("release", universePrefix)
              .withLabelNotIn("service-type", "headless", "non-endpoint")
              .list()
              .getItems();
      // TODO: PLAT-5625: This might need a change when we have one
      // common TServer/Master endpoint service across multiple Helm
      // releases. Currently we call getPreferredServiceIP for each AZ
      // deployment/Helm release, and return all the IPs.
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
  public void deleteStorage(Map<String, String> config, String helmReleaseName, String namespace) {
    try (KubernetesClient client = getClient(config)) {
      client
          .persistentVolumeClaims()
          .inNamespace(namespace)
          .withLabel("release", helmReleaseName)
          .delete();
    }
  }

  @Override
  public void deleteUnusedPVCs(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle,
      int replicaCount) {
    throw new UnsupportedOperationException("Unimplemented method 'deleteAllServerTypePods'");
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

  @Override
  public boolean deleteStatefulSet(Map<String, String> config, String namespace, String stsName) {
    try (KubernetesClient client = getClient(config)) {
      // We just check if the list of StatusDetails is empty or not.
      return client
          .apps()
          .statefulSets()
          .inNamespace(namespace)
          .withName(stsName)
          .withPropagationPolicy(DeletionPropagation.ORPHAN)
          .delete()
          .isEmpty();
    }
  }

  @Override
  public boolean expandPVC(
      UUID universeUUID,
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      String newDiskSize,
      boolean newNamingStyle) {
    String appLabel = newNamingStyle ? "app.kubernetes.io/name" : "app";
    Map<String, String> labels = ImmutableMap.of(appLabel, appName, "release", helmReleaseName);
    try (KubernetesClient client = getClient(config)) {
      List<PersistentVolumeClaim> pvcs =
          client
              .persistentVolumeClaims()
              .inNamespace(namespace)
              .withLabels(labels)
              .list()
              .getItems();
      for (PersistentVolumeClaim pvc : pvcs) {
        log.info("Updating PVC size for {} to {}", pvc.getMetadata().getName(), newDiskSize);
        pvc.getSpec().getResources().getRequests().put("storage", new Quantity(newDiskSize));
        // The .withName is so we can chain the .patch, an update to the client
        // seems to have changed it so that this is required.
        client
            .persistentVolumeClaims()
            .withName(pvc.getMetadata().getName())
            .patch(PatchContext.of(PatchType.STRATEGIC_MERGE), pvc);
      }
      return true;
    }
  }

  @Override
  public void copyFileToPod(
      Map<String, String> config,
      String namespace,
      String podName,
      String containerName,
      String srcFilePath,
      String destFilePath) {
    try (KubernetesClient client = getClient(config)) {
      client
          .pods()
          .inNamespace(namespace)
          .withName(podName)
          .inContainer(containerName)
          .file(destFilePath)
          .upload(Paths.get(srcFilePath));
    }
  }

  @Override
  public void performYbcAction(
      Map<String, String> config,
      String namespace,
      String podName,
      String containerName,
      List<String> commandArgs) {
    try (KubernetesClient client = getClient(config)) {
      ByteArrayOutputStream baos = new ByteArrayOutputStream();
      CompletableFuture<String> data = new CompletableFuture<>();
      client
          .pods()
          .inNamespace(namespace)
          .withName(podName)
          .inContainer(containerName)
          .writingOutput(baos)
          .writingError(baos)
          .usingListener(new SimpleListener(data, baos))
          .exec(commandArgs.stream().toArray(String[]::new));
    }
  }

  @Override
  public String getStorageClassName(
      Map<String, String> config,
      String namespace,
      String universePrefix,
      boolean forMaster,
      boolean newNamingStyle) {
    // TODO: Implement when switching to native client implementation
    return null;
  }

  @Override
  public boolean storageClassAllowsExpansion(Map<String, String> config, String storageClassName) {
    // TODO: Implement when switching to native client implementation
    return true;
  }

  @Override
  public void diff(Map<String, String> config, String inputYamlFilePath) {
    // TODO(anijhawan): Implement this when we get a chance.
  }

  @Override
  public String getCurrentContext(Map<String, String> azConfig) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String execCommandProcessErrors(Map<String, String> config, List<String> commandList) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getK8sResource(
      Map<String, String> config, String k8sResource, String namespace, String outputFormat) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getEvents(Map<String, String> config, String namespace, String string) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getK8sVersion(Map<String, String> config, String outputFormat) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getPlatformNamespace() {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getPlatformPodName() {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getHelmValues(
      Map<String, String> config, String namespace, String helmReleaseName, String outputFormat) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public List<RoleData> getAllRoleDataForServiceAccountName(
      Map<String, String> config, String serviceAccountName) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getServiceAccountPermissions(
      Map<String, String> config, RoleData roleData, String outputFormat) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getStorageClass(
      Map<String, String> config, String storageClassName, String outputFormat) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public StorageClass getStorageClass(Map<String, String> config, String storageClassName) {
    throw new UnsupportedOperationException("Unimplemented method 'getStorageClass'");
  }

  @Override
  public String getKubeconfigUser(Map<String, String> config) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public String getKubeconfigCluster(Map<String, String> config) {
    // TODO Auto-generated method stub
    return null;
  }

  @Override
  public List<PersistentVolumeClaim> getPVCs(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle) {
    // TODO Auto-generated method stub
    throw new UnsupportedOperationException("Unimplemented method 'getPVCNames'");
  }

  @Override
  public List<Quantity> getPVCSizeList(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle) {
    throw new UnsupportedOperationException("Unimplemented method 'getPVCNames'");
  }

  @Override
  public List<Pod> getPods(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle) {
    // TODO Auto-generated method stub
    throw new UnsupportedOperationException("Unimplemented method 'getPods'");
  }

  public void deleteAllServerTypePods(
      Map<String, String> config,
      String namespace,
      ServerType serverType,
      String releaseName,
      boolean newNamingStyle) {
    // TODO Auto-generated method stub
    throw new UnsupportedOperationException("Unimplemented method 'deleteAllServerTypePods'");
  }

  @Override
  public boolean resourceExists(
      Map<String, String> config, String resourceType, String resourceName, String namespace) {
    // TODO Auto-generated method stub
    throw new UnsupportedOperationException("Unimplemented method 'resourceExists'");
  }
}

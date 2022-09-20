// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.helm.HelmUtils;
import io.fabric8.kubernetes.api.model.LoadBalancerIngress;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodCondition;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.events.v1.Event;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public abstract class KubernetesManager {

  @Inject ReleaseManager releaseManager;

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject play.Configuration appConfig;

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesManager.class);

  private static final String LEGACY_HELM_CHART_FILENAME = "yugabyte-2.7-helm-legacy.tar.gz";

  private static final long DEFAULT_TIMEOUT_SECS = 300;

  /* helm interface */

  public void helmInstall(
      String ybSoftwareVersion,
      Map<String, String> config,
      UUID providerUUID,
      String universePrefix,
      String namespace,
      String overridesFile,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides) {

    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);

    List<String> commandArrayList =
        new ArrayList<>(
            Arrays.asList(
                "helm",
                "install",
                helmReleaseName,
                helmPackagePath,
                "--namespace",
                namespace,
                "-f",
                overridesFile,
                "--timeout",
                getTimeout(),
                "--wait"));
    LOG.info("After Overrides excluded - {}" + String.join(" ", commandArrayList));
    if (universeOverrides != null) {
      commandArrayList.addAll(setOverrides(HelmUtils.flattenMap(universeOverrides)));
    }
    if (azOverrides != null) {
      commandArrayList.addAll(setOverrides(HelmUtils.flattenMap(azOverrides)));
    }
    // TODO gflags we need to add here. Don't know the order yet.

    List<String> unmodifiableList = Collections.unmodifiableList(commandArrayList);
    ShellResponse response = execCommand(config, unmodifiableList);
    processHelmResponse(config, universePrefix, namespace, response);
  }

  private List<String> setOverrides(Map<String, String> overrides) {
    List<String> res = new ArrayList<>();
    for (Map.Entry<String, String> entry : overrides.entrySet()) {
      res.add("--set");
      res.add(String.format("%s=%s", entry.getKey(), entry.getValue()));
    }
    return res;
  }

  public void helmUpgrade(
      String ybSoftwareVersion,
      Map<String, String> config,
      String universePrefix,
      String namespace,
      String overridesFile,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides) {

    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);

    List<String> commandArrayList =
        new ArrayList<>(
            Arrays.asList(
                "helm",
                "upgrade",
                helmReleaseName,
                helmPackagePath,
                "-f",
                overridesFile,
                "--namespace",
                namespace,
                "--timeout",
                getTimeout(),
                "--wait"));
    LOG.info("After Overrides excluded - " + String.join(" ", commandArrayList));
    commandArrayList.addAll(setOverrides(HelmUtils.flattenMap(universeOverrides)));
    commandArrayList.addAll(setOverrides(HelmUtils.flattenMap(azOverrides)));
    // TODO gflags we need to add here. Don't know the order yet.

    List<String> unmodifiableList = Collections.unmodifiableList(commandArrayList);
    ShellResponse response = execCommand(config, unmodifiableList);
    processHelmResponse(config, universePrefix, namespace, response);
  }

  public void helmDelete(Map<String, String> config, String universePrefix, String namespace) {
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    List<String> commandList = ImmutableList.of("helm", "delete", helmReleaseName, "-n", namespace);
    execCommand(config, commandList);
  }

  public String helmShowValues(String ybSoftwareVersion, Map<String, String> config) {
    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
    List<String> commandList = ImmutableList.of("helm", "show", "values", helmPackagePath);
    LOG.info(String.join(" ", commandList));
    ShellResponse response = execCommand(config, commandList);
    if (response != null) {
      if (response.getCode() != ShellResponse.ERROR_CODE_SUCCESS) {
        throw new RuntimeException(response.getMessage());
      }
      return response.getMessage();
    }
    return null;
  }

  /* helm helpers */

  private void processHelmResponse(
      Map<String, String> config, String universePrefix, String namespace, ShellResponse response) {

    if (response != null && !response.isSuccess()) {

      try {
        List<Event> events = getEvents(config, namespace);

        LOG.info("Events in namespace {} : \n {} ", namespace, toReadableString(events));
      } catch (Exception ex) {
        LOG.warn("Ignoring error listing events in namespace {}: {}", namespace, ex);
      }
      String message;
      List<Pod> pods = getPodInfos(config, universePrefix, namespace);
      for (Pod pod : pods) {
        String podStatus = pod.getStatus().getPhase();
        if (!podStatus.equals("Running")) {
          for (PodCondition condition : pod.getStatus().getConditions()) {
            if (condition.getStatus().equals("False")) {
              message = condition.getMessage();
              throw new RuntimeException(message);
            }
          }
        }
      }
      if (pods.isEmpty()) {
        message = "No pods even scheduled. Previous step(s) incomplete";
      } else {
        message = "Pods are ready. Services still not running";
      }
      throw new RuntimeException(message);
    }
  }

  public String getTimeout() {
    Long timeout = appConfig.getLong("yb.helm.timeout_secs");
    if (timeout == null || timeout == 0) {
      timeout = DEFAULT_TIMEOUT_SECS;
    }
    return String.valueOf(timeout) + "s";
  }

  private ShellResponse execCommand(Map<String, String> config, List<String> command) {
    String description = String.join(" ", command);
    return shellProcessHandler.run(command, config, description);
  }

  public String getHelmPackagePath(String ybSoftwareVersion) {
    String helmPackagePath = null;

    // Get helm package filename from release metadata.
    ReleaseManager.ReleaseMetadata releaseMetadata =
        releaseManager.getReleaseByVersion(ybSoftwareVersion);
    if (releaseMetadata != null) {
      helmPackagePath = releaseMetadata.chartPath;
    }

    if (helmPackagePath == null || helmPackagePath.isEmpty()) {
      // TODO: The "legacy" helm chart is included in the yugaware container build to ensure that
      // universes deployed using previous versions of the platform (that did not use versioned
      // helm charts) will still be usable after upgrading to newer versions of the platform (that
      // use versioned helm charts). We can (and should) remove this special case once all customers
      // that use the k8s provider have upgraded their platforms and universes to versions > 2.7.
      if (Util.compareYbVersions(ybSoftwareVersion, "2.8.0.0") < 0) {
        helmPackagePath =
            new File(appConfig.getString("yb.helm.packagePath"), LEGACY_HELM_CHART_FILENAME)
                .toString();
      } else {
        throw new RuntimeException("Helm Package path not found for release: " + ybSoftwareVersion);
      }
    }

    // Ensure helm package file actually exists.
    File helmPackage = new File(helmPackagePath);
    if (!helmPackage.exists()) {
      throw new RuntimeException("Helm Package file not found: " + helmPackagePath);
    }

    return helmPackagePath;
  }

  /* kubernetes helpers */

  protected static String getIp(Service service) {
    if (service.getStatus() != null
        && service.getStatus().getLoadBalancer() != null
        && service.getStatus().getLoadBalancer().getIngress() != null
        && !service.getStatus().getLoadBalancer().getIngress().isEmpty()) {
      LoadBalancerIngress ingress = service.getStatus().getLoadBalancer().getIngress().get(0);
      if (ingress.getHostname() != null) {
        return ingress.getHostname();
      }
      if (ingress.getIp() != null) {
        return ingress.getIp();
      }
    }

    if (service.getSpec() != null && service.getSpec().getClusterIP() != null) {
      return service.getSpec().getClusterIP();
    }
    return null;
  }

  private String toReadableString(Event event) {
    return event.getAdditionalProperties().getOrDefault("firstTimestamp", "null first ts")
        + " , "
        + event.getAdditionalProperties().getOrDefault("lastTimestamp", "null last ts")
        + " , "
        + event.getAdditionalProperties().getOrDefault("message", "null msg")
        + " , "
        + event.getAdditionalProperties().getOrDefault("involvedObject", "null obj");
  }

  private String toReadableString(List<Event> events) {
    return events.stream().map(x -> toReadableString(x)).collect(Collectors.joining("\n"));
  }

  /* kubernetes interface */

  public abstract void createNamespace(Map<String, String> config, String universePrefix);

  public abstract void applySecret(Map<String, String> config, String namespace, String pullSecret);

  public abstract List<Pod> getPodInfos(
      Map<String, String> config, String universePrefix, String namespace);

  public abstract List<Service> getServices(
      Map<String, String> config, String universePrefix, String namespace);

  public abstract PodStatus getPodStatus(
      Map<String, String> config, String namespace, String podName);

  /** @return the first that exists of loadBalancer.hostname, loadBalancer.ip, clusterIp */
  public abstract String getPreferredServiceIP(
      Map<String, String> config,
      String universePrefix,
      String namespace,
      boolean isMaster,
      boolean newNamingStyle);

  public abstract List<Node> getNodeInfos(Map<String, String> config);

  public abstract Secret getSecret(
      Map<String, String> config, String secretName, @Nullable String namespace);

  public abstract void updateNumNodes(
      Map<String, String> config,
      String universePrefix,
      String namespace,
      int numNodes,
      boolean newNamingStyle);

  public abstract void deleteStorage(
      Map<String, String> config, String universePrefix, String namespace);

  public abstract void deleteNamespace(Map<String, String> config, String namespace);

  public abstract void deletePod(Map<String, String> config, String namespace, String podName);

  public abstract List<Event> getEvents(Map<String, String> config, String namespace);
}

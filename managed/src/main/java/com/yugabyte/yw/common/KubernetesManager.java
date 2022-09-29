// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Sets;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.forms.KubernetesOverridesResponse;
import io.fabric8.kubernetes.api.model.LoadBalancerIngress;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodCondition;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.events.v1.Event;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yaml.snakeyaml.Yaml;

public abstract class KubernetesManager {

  @Inject ReleaseManager releaseManager;

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject play.Configuration appConfig;

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesManager.class);

  private static final String LEGACY_HELM_CHART_FILENAME = "yugabyte-2.7-helm-legacy.tar.gz";

  private static final long DEFAULT_TIMEOUT_SECS = 300;

  private static final long DEFAULT_HELM_TEMPLATE_TIMEOUT_SECS = 1;

  /* helm interface */

  public void helmInstall(
      String ybSoftwareVersion,
      Map<String, String> config,
      UUID providerUUID,
      String universePrefix,
      String namespace,
      String overridesFile) {

    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    List<String> commandList =
        ImmutableList.of(
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
            "--wait");
    ShellResponse response = execCommand(config, commandList);
    processHelmResponse(config, universePrefix, namespace, response);
  }

  public void helmUpgrade(
      String ybSoftwareVersion,
      Map<String, String> config,
      String universePrefix,
      String namespace,
      String overridesFile) {
    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
    String helmReleaseName = Util.sanitizeHelmReleaseName(universePrefix);
    List<String> commandList =
        ImmutableList.of(
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
            "--wait");
    ShellResponse response = execCommand(config, commandList);
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

  // userMap - flattened user provided yaml. valuesMap - flattened chart's values yaml.
  // Return userMap keys which are not in valuesMap.
  // It doesn't check if returnred keys are actually not present in chart templates.
  private Set<String> getUknownKeys(Map<String, String> userMap, Map<String, String> valuesMap) {
    return Sets.difference(userMap.keySet(), valuesMap.keySet());
  }

  private Set<String> getNullValueKeys(Map<String, String> userMap) {
    return userMap
        .entrySet()
        .stream()
        .filter(e -> e.getValue() == null)
        .map(Map.Entry::getKey)
        .collect(Collectors.toSet());
  }

  // Checks if override keys are present in values.yaml in the chart.
  // Runs helm template.
  // Returns K8sOverridesResponse = {universeOverridesErrors, azOverridesErrors,
  // helmTemplateErrors}.
  public Set<String> validateOverrides(
      String ybSoftwareVersion,
      Map<String, String> config,
      String namespace,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      String azCode) {
    Set<String> errorsSet = new HashSet<>();
    // TODO optimise this step. It is called for every AZ.
    String helmPackagePath, helmValuesStr;
    try {
      helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
      helmValuesStr = helmShowValues(ybSoftwareVersion, null);
    } catch (Exception e) {
      String errMsg = String.format("Helm package/values fetch failed: %s", e.getMessage());
      LOG.error("Error during validation : failed to get helm package path: ", e);
      errorsSet.add(errMsg);
      return errorsSet;
    }
    Map<String, String> flatHelmValues =
        HelmUtils.flattenMap(HelmUtils.convertYamlToMap(helmValuesStr));
    Map<String, String> flatUnivOverrides = HelmUtils.flattenMap(universeOverrides);
    Map<String, String> flatAZOverrides = HelmUtils.flattenMap(azOverrides);

    Set<String> universeOverridesUnknownKeys = getUknownKeys(flatUnivOverrides, flatHelmValues);
    Set<String> universeOverridesNullValueKeys = getNullValueKeys(flatUnivOverrides);
    Set<String> azOverridesUnknownKeys = getUknownKeys(flatAZOverrides, flatHelmValues);
    Set<String> azOverridesNullValueKeys = getNullValueKeys(flatAZOverrides);

    if (universeOverridesUnknownKeys.size() != 0) {
      errorsSet.add(
          String.format(
              "Unknown keys found in universe overrides: %s.", universeOverridesUnknownKeys));
    }
    if (universeOverridesNullValueKeys.size() != 0) {
      errorsSet.add(
          String.format(
              "Found null values in universe overrides, "
                  + "please double check if this is intentional: %s.",
              universeOverridesNullValueKeys));
    }

    if (azOverridesUnknownKeys.size() != 0) {
      errorsSet.add(
          String.format(
              "%s: Unknown keys found in az overrides: %s.", azCode, azOverridesUnknownKeys));
    }
    if (azOverridesNullValueKeys.size() != 0) {
      errorsSet.add(
          String.format(
              "%s: Found null values in az overrides, "
                  + "please double check if this is intentional: %s.",
              azCode, azOverridesNullValueKeys));
    }

    HelmUtils.mergeYaml(azOverrides, universeOverrides);
    String mergedOverrides = createTempFile(azOverrides);

    List<String> commandList =
        ImmutableList.of(
            "helm",
            "template",
            "yb-validate-k8soverrides" /* dummy name */,
            helmPackagePath,
            "-f",
            mergedOverrides,
            "--namespace",
            namespace,
            "--timeout",
            String.valueOf(DEFAULT_HELM_TEMPLATE_TIMEOUT_SECS) + "s",
            "--wait");
    // TODO some gflags have precedence over overrides. We need to add them here. Don't know the
    // order yet.
    ShellResponse response = execCommand(config, commandList);
    if (!response.isSuccess()) {
      // result.extractRunCommandOutput() is not working. We always get "Invalid command output"
      String helmError = response.message;
      // Most helm errors start with "Error:""
      String helmErrorPrefix = "Error:";
      int errorIndex = helmError.indexOf(helmErrorPrefix);
      if (errorIndex != -1) {
        errorsSet.add(helmError.substring(errorIndex + helmErrorPrefix.length()));
      } else {
        errorsSet.add(String.format("%s: %s", azCode, response.message));
      }
    }
    return errorsSet;
  }

  /* helm helpers */

  private String createTempFile(Map<String, Object> valuesMap) {
    Path tempFile;
    try {
      tempFile = Files.createTempFile("values", ".yml");
      try (BufferedWriter bw = new BufferedWriter(new FileWriter(tempFile.toFile())); ) {
        new Yaml().dump(valuesMap, bw);
        return tempFile.toAbsolutePath().toString();
      }
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Error writing values map to temp file!");
    }
  }

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

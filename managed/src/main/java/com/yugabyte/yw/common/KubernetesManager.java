// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.LoadBalancerIngress;
import io.fabric8.kubernetes.api.model.Namespace;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.PersistentVolumeClaim;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodCondition;
import io.fabric8.kubernetes.api.model.PodStatus;
import io.fabric8.kubernetes.api.model.Quantity;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.events.v1.Event;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.ToString;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yaml.snakeyaml.Yaml;

public abstract class KubernetesManager {

  @Inject ReleaseManager releaseManager;

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject RuntimeConfGetter confGetter;

  @Inject Config appConfig;

  @Inject FileHelperService fileHelperService;

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesManager.class);

  private static final String LEGACY_HELM_CHART_FILENAME = "yugabyte-2.7-helm-legacy.tar.gz";

  private static final long DEFAULT_TIMEOUT_SECS = 300;

  private static final long DEFAULT_HELM_TEMPLATE_TIMEOUT_SECS = 1;

  /* helm interface */

  public void helmInstall(
      UUID universeUUID,
      String ybSoftwareVersion,
      Map<String, String> config,
      UUID providerUUID,
      String helmReleaseName,
      String namespace,
      String overridesFile) {

    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);

    // List Helm releases to check if the release already exists
    List<String> listCmd = ImmutableList.of("helm", "list", "--short", "--namespace", namespace);

    ShellResponse responseList = execCommand(config, listCmd);
    responseList.processErrors();

    boolean helmReleaseExists = false;
    String output = responseList.getMessage();
    LOG.info("helm list command output {} ", output);
    if (output.contains(helmReleaseName)) {
      // The release already exists
      helmReleaseExists = true;
    }
    if (helmReleaseExists) {
      List<String> deleteCmd =
          ImmutableList.of(
              "helm", "uninstall", helmReleaseName, "--wait", "--namespace", namespace);
      ShellResponse responseDelete = execCommand(config, deleteCmd);
      responseDelete.processErrors();
    }

    List<String> commandList =
        ImmutableList.of(
            "helm",
            "install",
            helmReleaseName,
            helmPackagePath,
            "--debug",
            "--namespace",
            namespace,
            "-f",
            overridesFile,
            "--timeout",
            getTimeout(universeUUID),
            "--wait");
    ShellResponse response = execCommand(config, commandList);
    processHelmResponse(config, helmReleaseName, namespace, response);
  }

  // Log a diff before applying helm upgrade.
  public void diff(Map<String, String> config, String inputYamlFilePath) {
    List<String> diffCommandList =
        ImmutableList.of("kubectl", "diff", "--server-side=false", "-f ", inputYamlFilePath);
    ShellResponse response = execCommand(config, diffCommandList);
    if (response != null && !response.isSuccess()) {
      LOG.error("kubectl diff failed with response %s", response.toString());
    }
  }

  public String helmTemplate(
      UUID universeUuid,
      String ybSoftwareVersion,
      Map<String, String> config,
      String helmReleaseName,
      String namespace,
      String overridesFile) {
    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);

    Path tempOutputFile = fileHelperService.createTempFile("helm-template", ".output");
    String tempOutputPath = tempOutputFile.toAbsolutePath().toString();
    List<String> templateCommandList =
        ImmutableList.of(
            "helm",
            "template",
            helmReleaseName,
            helmPackagePath,
            "-f",
            overridesFile,
            "--namespace",
            namespace,
            "--timeout",
            getTimeout(universeUuid),
            "--is-upgrade",
            "--no-hooks",
            "--skip-crds",
            ">",
            tempOutputPath);

    ShellResponse response = execCommand(config, templateCommandList);
    if (response != null && !response.isSuccess()) {
      try {
        String templateOutput = Files.readAllLines(tempOutputFile).get(0);
        LOG.error("Output from the template command {}", templateOutput);
      } catch (Exception ex) {
        LOG.error("Got exception in reading template output {}", ex.getMessage());
      }

      return null;
    }

    // Success case return the output file path.
    return tempOutputPath;
  }

  public void helmUpgrade(
      UUID universeUuid,
      String ybSoftwareVersion,
      Map<String, String> config,
      String helmReleaseName,
      String namespace,
      String overridesFile) {
    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);

    // Capture the diff what is going to be upgraded.
    String helmTemplatePath =
        helmTemplate(
            universeUuid, ybSoftwareVersion, config, helmReleaseName, namespace, overridesFile);
    if (helmTemplatePath != null) {
      diff(config, helmTemplatePath);
    } else {
      LOG.error("Error in helm template generation");
    }

    List<String> commandList =
        ImmutableList.of(
            "helm",
            "upgrade",
            helmReleaseName,
            helmPackagePath,
            "--debug",
            "-f",
            overridesFile,
            "--namespace",
            namespace,
            "--timeout",
            getTimeout(universeUuid),
            "--wait");
    ShellResponse response = execCommand(config, commandList);
    processHelmResponse(config, helmReleaseName, namespace, response);
  }

  public void helmDelete(Map<String, String> config, String helmReleaseName, String namespace) {
    List<String> commandList = ImmutableList.of("helm", "delete", helmReleaseName, "-n", namespace);
    execCommand(config, commandList);
  }

  public String helmShowValues(String ybSoftwareVersion, Map<String, String> config) {
    String helmPackagePath = this.getHelmPackagePath(ybSoftwareVersion);
    List<String> commandList = ImmutableList.of("helm", "show", "values", helmPackagePath);
    LOG.info(String.join(" ", commandList));
    ShellResponse response = execCommand(config, commandList, false);
    if (response != null) {
      if (response.getCode() != ShellResponse.ERROR_CODE_SUCCESS) {
        throw new RuntimeException(response.getMessage());
      }
      return response.getMessage();
    }
    return null;
  }

  // userMap - user provided yaml. valuesMap - chart's values yaml.
  // Return userMap keys which are not in valuesMap.
  // It doesn't check if returnred keys are actually not present in chart templates.
  private Set<String> getUknownKeys(Map<String, Object> userMap, Map<String, Object> valuesMap) {
    return Sets.difference(userMap.keySet(), valuesMap.keySet());
  }

  // Returns values we applied. Excludes values.yaml entries unless they were overriden.
  public String getOverridenHelmReleaseValues(
      String namespace, String helmReleaseName, Map<String, String> config) {
    List<String> commandList =
        ImmutableList.of("helm", "get", "values", helmReleaseName, "-n", namespace, "-o", "yaml");
    LOG.info(String.join(" ", commandList));
    ShellResponse response = execCommand(config, commandList);
    if (response != null) {
      if (response.getCode() != ShellResponse.ERROR_CODE_SUCCESS) {
        throw new RuntimeException(response.getMessage());
      }
      return response.getMessage();
    }
    LOG.error(
        String.format(
            "Helm get values response is null for helm release: %s in namespace: %s",
            helmReleaseName, namespace));
    throw new RuntimeException("Helm get values response is null");
  }

  private Set<String> getNullValueKeys(Map<String, String> userMap) {
    return userMap.entrySet().stream()
        .filter(e -> e.getValue() == null)
        .map(Map.Entry::getKey)
        .collect(Collectors.toSet());
  }

  // Checks if override keys are present in values.yaml in the chart.
  // Runs helm template.
  // Returns set of error strings.
  public Set<String> validateOverrides(
      String ybSoftwareVersion,
      Map<String, String> config,
      String namespace,
      Map<String, Object> universeOverrides,
      Map<String, Object> azOverrides,
      String azCode) {
    Set<String> errorsSet = new HashSet<>();
    Map<String, Object> universeOverridesCopy = new HashMap<>();
    Map<String, Object> azOverridesCopy = new HashMap<>();
    ObjectMapper mapper = new ObjectMapper();
    String universeOverridesString = "", azOverridesString = "";
    try {
      universeOverridesString = mapper.writeValueAsString(universeOverrides);
      universeOverridesCopy = mapper.readValue(universeOverridesString, Map.class);
      azOverridesString = mapper.writeValueAsString(azOverrides);
      azOverridesCopy = mapper.readValue(azOverridesString, Map.class);
    } catch (IOException e) {
      LOG.error(
          String.format(
              "Error in writing overrides map to string or string to map: "
                  + "universe overrides: %s, azOverrides: %s",
              universeOverrides, azOverrides),
          e);
      errorsSet.add("Error parsing one of the overrides");
      return errorsSet;
    }
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
    Map<String, Object> helmValues = HelmUtils.convertYamlToMap(helmValuesStr);
    Map<String, String> flatUnivOverrides = HelmUtils.flattenMap(universeOverridesCopy);
    Map<String, String> flatAZOverrides = HelmUtils.flattenMap(azOverridesCopy);

    Set<String> universeOverridesUnknownKeys = getUknownKeys(universeOverridesCopy, helmValues);
    Set<String> universeOverridesNullValueKeys = getNullValueKeys(flatUnivOverrides);
    Set<String> azOverridesUnknownKeys = getUknownKeys(azOverridesCopy, helmValues);
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

    HelmUtils.mergeYaml(universeOverridesCopy, azOverridesCopy);
    String mergedOverrides = createTempFile(universeOverridesCopy);

    List<String> commandList =
        ImmutableList.of(
            "helm",
            "template",
            "yb-validate-k8soverrides" /* dummy name */,
            "--debug",
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
      tempFile = fileHelperService.createTempFile("values", ".yml");
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

  public Long getTimeoutSecs(UUID universeUuid) {
    Long timeout =
        confGetter.getConfForScope(
            Universe.getOrBadRequest(universeUuid), UniverseConfKeys.helmTimeoutSecs);
    if (timeout == null || timeout == 0) {
      timeout = DEFAULT_TIMEOUT_SECS;
    }
    return timeout;
  }

  public String getTimeout(UUID universeUuid) {
    return String.valueOf(getTimeoutSecs(universeUuid)) + "s";
  }

  private ShellResponse execCommand(
      Map<String, String> config, List<String> command, boolean logCmdOutput) {
    String description = String.join(" ", command);
    return shellProcessHandler.run(command, config, logCmdOutput, description);
  }

  private ShellResponse execCommand(Map<String, String> config, List<String> command) {
    return execCommand(config, command, true);
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

  /**
   * Checks if the given kubeConfig points to the same Kubernetes cluster where YBA is running. This
   * is done by checking existance of the YBA pod in the given cluster.
   *
   * @param kubeConfig the kubeConfig of the cluster to check.
   * @return true if it is home cluster, false if pod is not found.
   */
  public boolean isHomeCluster(String kubeConfig) {
    if (StringUtils.isBlank(kubeConfig)) {
      return true;
    }

    String ns = getPlatformNamespace();
    String podName = getPlatformPodName();

    // TODO(bhavin192): verify the .metadata.uid of the pod object to
    // ensure it is the same pod.
    // Map<String, String> homeConfig = ImmutableMap.of("KUBECONFIG", "");
    // Pod homePod = k8s.getPodObject(homeConfig, ns, podName);

    Map<String, String> config = ImmutableMap.of("KUBECONFIG", kubeConfig);
    try {
      getPodObject(config, ns, podName);
    } catch (RuntimeException e) {
      if (e.getMessage().contains("Error from server (NotFound): namespaces")
          || e.getMessage().contains("Error from server (NotFound): pods")) {
        return false;
      }
      throw e;
    }
    return true;
  }

  /* kubernetes interface */

  public abstract void createNamespace(Map<String, String> config, String universePrefix);

  public abstract void applySecret(Map<String, String> config, String namespace, String pullSecret);

  public abstract Pod getPodObject(Map<String, String> config, String namespace, String podName);

  public abstract String getCloudProvider(Map<String, String> config);

  public abstract List<Pod> getPodInfos(
      Map<String, String> config, String universePrefix, String namespace);

  public abstract List<Service> getServices(
      Map<String, String> config, String universePrefix, String namespace);

  public abstract List<Namespace> getNamespaces(Map<String, String> config);

  public boolean namespaceExists(Map<String, String> config, String namespace) {
    Set<String> namespaceNames =
        getNamespaces(config).stream()
            .map(n -> n.getMetadata().getName())
            .collect(Collectors.toSet());
    return namespaceNames.contains(namespace);
  }

  public abstract PodStatus getPodStatus(
      Map<String, String> config, String namespace, String podName);

  /**
   * @return the first that exists of loadBalancer.hostname, loadBalancer.ip, clusterIp
   */
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

  public abstract boolean deleteStatefulSet(
      Map<String, String> config, String namespace, String stsName);

  public abstract boolean expandPVC(
      UUID universeUUID,
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      String newDiskSize,
      boolean newNamingStyle);

  public abstract List<Quantity> getPVCSizeList(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle);

  public abstract List<PersistentVolumeClaim> getPVCs(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle);

  public abstract List<Pod> getPods(
      Map<String, String> config,
      String namespace,
      String helmReleaseName,
      String appName,
      boolean newNamingStyle);

  public abstract void deleteAllServerTypePods(
      Map<String, String> config,
      String namespace,
      ServerType serverType,
      String releaseName,
      boolean newNamingStyle);

  public abstract void copyFileToPod(
      Map<String, String> config,
      String namespace,
      String podName,
      String containerName,
      String srcFilePath,
      String destFilePath);

  public abstract void performYbcAction(
      Map<String, String> config,
      String namespace,
      String podName,
      String containerName,
      List<String> commandArgs);

  // Get the name of StorageClass used for master/tserver PVCs.
  public abstract String getStorageClassName(
      Map<String, String> config,
      String namespace,
      String universePrefix,
      boolean forMaster,
      boolean newNamingStyle);

  public abstract boolean storageClassAllowsExpansion(
      Map<String, String> config, String storageClassName);

  public abstract String getCurrentContext(Map<String, String> azConfig);

  public abstract String execCommandProcessErrors(
      Map<String, String> config, List<String> commandList);

  public abstract String getK8sResource(
      Map<String, String> config, String k8sResource, String namespace, String outputFormat);

  public abstract String getEvents(Map<String, String> config, String namespace, String string);

  public abstract String getK8sVersion(Map<String, String> config, String outputFormat);

  public abstract String getPlatformNamespace();

  public abstract String getPlatformPodName();

  public abstract String getHelmValues(
      Map<String, String> config, String namespace, String helmReleaseName, String outputFormat);

  @Data
  @ToString(includeFieldNames = true)
  @AllArgsConstructor
  public static class RoleData {
    public String kind;
    public String name;
    public String namespace;
  }

  public abstract List<RoleData> getAllRoleDataForServiceAccountName(
      Map<String, String> config, String serviceAccountName);

  public abstract String getServiceAccountPermissions(
      Map<String, String> config, RoleData roleData, String outputFormat);

  public abstract String getStorageClass(
      Map<String, String> config, String storageClassName, String namespace, String outputFormat);

  public abstract String getKubeconfigUser(Map<String, String> config);

  public abstract String getKubeconfigCluster(Map<String, String> config);
}

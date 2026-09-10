package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.common.supportbundle.SupportBundleFormDataV2Mapper;
import com.yugabyte.yw.common.supportbundle.SupportBundleV2K8sSupport;
import com.yugabyte.yw.common.supportbundle.SupportBundleV2NodeSelector;
import com.yugabyte.yw.common.supportbundle.SupportBundleV2SpecValidator;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.forms.SupportBundleSizeEstimateResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.PromExportType;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class SupportBundleHandlerV2 {

  private final SupportBundleUtil supportBundleUtil;
  private final RuntimeConfGetter confGetter;
  private final SupportBundleComponentFactory componentFactory;
  private final Config staticConf;
  private final PlatformExecutorFactory executorFactory;
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleV2SpecValidator specValidator;
  private final SupportBundleV2NodeSelector nodeSelector;

  @Inject
  public SupportBundleHandlerV2(
      SupportBundleUtil supportBundleUtil,
      RuntimeConfGetter confGetter,
      SupportBundleComponentFactory componentFactory,
      Config staticConf,
      PlatformExecutorFactory executorFactory,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleV2SpecValidator specValidator,
      SupportBundleV2NodeSelector nodeSelector) {
    this.supportBundleUtil = supportBundleUtil;
    this.confGetter = confGetter;
    this.componentFactory = componentFactory;
    this.staticConf = staticConf;
    this.executorFactory = executorFactory;
    this.nodeUniverseManager = nodeUniverseManager;
    this.specValidator = specValidator;
    this.nodeSelector = nodeSelector;
  }

  public void bundleDataValidationYbaOnly(SupportBundleFormDataV2 bundleData) {
    if (bundleData.components == null || bundleData.components.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "'components' must be non-empty.");
    }
    if (!bundleData.isYbaComponentOnly()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "YBA-only support bundles must request only the YBAComponent type. Node-level or"
              + " universe-scoped global components require a universe.");
    }
    validateYbaComponent("YBAComponent", "ybaComponentSpecs", bundleData.ybaComponentSpecs);

    if (CollectionUtils.isNotEmpty(bundleData.nodeNames)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "'nodeNames' is not applicable to YBA-only support bundles, which collect no node-level"
              + " components.");
    }

    if (bundleData.startDate != null
        && bundleData.endDate != null
        && !supportBundleUtil.checkDatesValid(bundleData.startDate, bundleData.endDate)) {
      throw new PlatformServiceException(BAD_REQUEST, "'startDate' should be before the 'endDate'");
    }
  }

  /**
   * Validate support bundle form and throw BAD_REQUEST if validation fails.
   *
   * @param bundleData
   * @param universe
   */
  public void bundleDataValidation(SupportBundleFormDataV2 bundleData, Universe universe) {
    // Support bundle for onprem and k8s universes was originally behind a runtime flag.
    // Now both are enabled by default.
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    Boolean k8sEnabled = confGetter.getGlobalConf(GlobalConfKeys.supportBundleK8sEnabled);
    Boolean onpremEnabled = confGetter.getGlobalConf(GlobalConfKeys.supportBundleOnPremEnabled);
    Boolean allowCoresCollection =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleAllowCoresCollection);
    if (CloudType.onprem.equals(cloudType) && !onpremEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Creating support bundle for on-prem universes is not enabled. "
              + "Please set onprem_enabled=true to create support bundle");
    }
    if (CloudType.kubernetes.equals(cloudType) && !k8sEnabled) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Creating support bundle for k8s universes is not enabled. "
              + "Please set k8s_enabled=true to create support bundle");
    }

    if (cloudType != CloudType.kubernetes
        && bundleData.components.contains(ComponentType.K8sInfo)) {
      bundleData.components.remove(ComponentType.K8sInfo);
      log.warn(
          "Component 'K8sInfo' is only applicable for kubernetes universes, not cloud type = "
              + cloudType.toString()
              + ". Continuing without it.");
    }

    if (bundleData.components.contains(ComponentType.SystemLogs)
        && cloudType.equals(CloudType.kubernetes)) {
      bundleData.components.remove(ComponentType.SystemLogs);
      log.warn(
          "Component 'SystemLogs' is not applicable for kubernetes universes. Continuing without"
              + " it.");
    }

    // Strip whatever a pod cannot produce before the specs are validated, so that BundleDetails and
    // the bundle's manifest.json name only what is really collected.
    if (CloudType.kubernetes.equals(cloudType)) {
      pruneK8sUnsupportedSpecs(bundleData);
      pruneK8sUnsupportedPromExports(bundleData);
      if (bundleData.components.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Nothing in this request can be collected from a kubernetes universe. The"
                    + " sub-components %s run outside the yb-master / yb-tserver pod and are not"
                    + " supported there.",
                SupportBundleV2K8sSupport.K8S_UNSUPPORTED_COMPONENTS.stream()
                    .sorted()
                    .collect(Collectors.joining(", "))));
      }
    }

    // Narrow the request down to the nodes that actually exist so that everything persisted from
    // here on (BundleDetails, and the manifest.json inside the bundle) names only the nodes that
    // were really targeted. Throws when nothing matched.
    if (CollectionUtils.isNotEmpty(bundleData.nodeNames)) {
      bundleData.nodeNames =
          SupportBundleV2NodeSelector.nodeNamesOf(
              nodeSelector.resolveOrThrow(universe, bundleData.nodeNames));
    }

    if (bundleData.components.contains(ComponentType.FilesComponent)) {
      validateScriptTarSpecs(
          "FilesComponent", "filesComponentSpecs", bundleData.filesComponentSpecs, universe);
    }

    if (bundleData.components.contains(ComponentType.BashComponent)) {
      validateScriptTarSpecs(
          "BashComponent", "bashComponentSpecs", bundleData.bashComponentSpecs, universe);
    }

    if (bundleData.components.contains(ComponentType.YSQLComponent)) {
      validateNodeCommandComponent("YSQLComponent", bundleData.ysqlComponentSpecs);
      bundleData.ysqlComponentSpecs.forEach(
          spec -> {
            if (spec == null || spec.getQueries() == null || spec.getQueries().isEmpty()) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Each YSQLComponent spec requires a non-empty 'queries' list.");
            }
            String specLabel =
                org.apache.commons.lang3.StringUtils.defaultIfBlank(
                    spec.getComponentName(), "YSQLComponent");
            specValidator.validateFileNameSegment(
                "YSQLComponent", "componentName", spec.getComponentName());
            specValidator.validateFileNameSegment(
                specLabel, "outputFileName", spec.getOutputFileName());
            specValidator.validateYsqlQueries(specLabel, spec.getQueries());
          });
    }

    if (bundleData.components.contains(ComponentType.YCQLComponent)) {
      validateNodeCommandComponent("YCQLComponent", bundleData.ycqlComponentSpecs);
      bundleData.ycqlComponentSpecs.forEach(
          spec -> {
            if (spec == null || spec.getQueries() == null || spec.getQueries().isEmpty()) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Each YCQLComponent spec requires a non-empty 'queries' list.");
            }
            String specLabel =
                org.apache.commons.lang3.StringUtils.defaultIfBlank(
                    spec.getComponentName(), "YCQLComponent");
            specValidator.validateFileNameSegment(
                "YCQLComponent", "componentName", spec.getComponentName());
            specValidator.validateFileNameSegment(
                specLabel, "outputFileName", spec.getOutputFileName());
            specValidator.validateYcqlQueries(specLabel, spec.getQueries());
          });
    }

    if (bundleData.components.contains(ComponentType.YbAdminComponent)) {
      validateNodeCommandComponent("YbAdminComponent", bundleData.ybAdminComponentSpecs);
      bundleData.ybAdminComponentSpecs.forEach(
          spec -> {
            if (spec == null
                || org.apache.commons.lang3.StringUtils.isBlank(spec.getYbAdminCommand())) {
              throw new PlatformServiceException(
                  BAD_REQUEST, "Each YbAdminComponent spec requires 'ybAdminCommand'.");
            }
            String specLabel =
                org.apache.commons.lang3.StringUtils.defaultIfBlank(
                    spec.getComponentName(), "YbAdminComponent");
            specValidator.validateFileNameSegment(
                "YbAdminComponent", "componentName", spec.getComponentName());
            specValidator.validateFileNameSegment(
                specLabel, "outputFileName", spec.getOutputFileName());
            specValidator.validateYbAdminCommands(specLabel, List.of(spec.getYbAdminCommand()));
          });
    }

    if (bundleData.components.contains(ComponentType.YBAComponent)) {
      validateYbaComponent("YBAComponent", "ybaComponentSpecs", bundleData.ybaComponentSpecs);
    }

    if (bundleData.components.contains(ComponentType.CoreFiles) && !allowCoresCollection) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Core file collection is disabled globally. Either remove core files component from"
              + " bundle creation, or enable runtime config"
              + " 'yb.support_bundle.allow_cores_collection'.");
    }

    if (bundleData.components.contains(ComponentType.PrometheusMetrics)
        && ((bundleData.promDumpStartDate == null) ^ (bundleData.promDumpEndDate == null))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Either define both 'promDumpStartDate' and 'promDumpEndDate', or neither (Will default"
              + " to 'yb.support_bundle.default_prom_dump_range' in this case)");
    }

    if (bundleData.startDate != null
        && bundleData.endDate != null
        && !supportBundleUtil.checkDatesValid(bundleData.startDate, bundleData.endDate)) {
      throw new PlatformServiceException(BAD_REQUEST, "'startDate' should be before the 'endDate'");
    }

    if (bundleData.components.contains(ComponentType.PrometheusMetrics)
        && bundleData.promDumpStartDate != null
        && bundleData.promDumpEndDate != null
        && !supportBundleUtil.checkDatesValid(
            bundleData.promDumpStartDate, bundleData.promDumpEndDate)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "'promDumpStartDate' should be before the 'promDumpEndDate'");
    }

    if (bundleData.components.contains(ComponentType.PrometheusMetrics)
        && bundleData.promExportType == PromExportType.REMOTE_READ
        && bundleData.promQueries != null
        && !bundleData.promQueries.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Custom Prometheus queries (promQueries) are not supported when promExportType is"
              + " REMOTE_READ. Clear promQueries or set promExportType to PROMQL.");
    }

    if (bundleData.components.contains(ComponentType.PrometheusMetrics)
        && (bundleData.promExportType == null || bundleData.promExportType == PromExportType.PROMQL)
        && bundleData.promMetricsFormat == PrometheusMetricsFormat.PROM_CHUNK) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Binary format is not supported when promExportType is PROMQL. "
              + "Clear promMetricsFormat or set promMetricsFormat to PROMQL_JSON.");
    }

    if (bundleData.components.contains(ComponentType.PrometheusMetrics)
        && (bundleData.promExportType == null || bundleData.promExportType == PromExportType.PROMQL)
        && !bundleData.promDumpDownSample) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Export without downsampling is not allowed when promExportType is PROMQL. "
              + "Set promDumpDownSample to true or use REMOTE_READ export method.");
    }

    // Vaidate that the given query names can be used as dir.
    bundleData
        .promQueries
        .keySet()
        .forEach(
            queryName -> {
              try {
                Paths.get(queryName);
              } catch (Exception e) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "Invalid query name: " + queryName + " in prom queries!");
              }
            });
  }

  /**
   * Calculate the size of a support bundle. For each given component we collect file sizes in
   * parallel. Only few node level components actually require connecting to the db nodes. When
   * required, only a single "find | ls" command is run on the node per component so this should be
   * safe and shouldn't affect cpu/memory consumption on the node.
   *
   * @return a map containing sizes for all components and the totalSize
   */
  public SupportBundleSizeEstimateResponse estimateBundleSize(
      Customer customer, SupportBundleFormDataV2 bundleData, Universe universe) throws Exception {

    SupportBundleFormData v1FormData =
        SupportBundleFormDataV2Mapper.INSTANCE.toV1FormData(bundleData);
    // Map to track sizes for all components.
    Map<String, Map<String, Long>> resp = new HashMap<>();
    Pair<Date, Date> datePair =
        supportBundleUtil.getValidStartAndEndDates(
            staticConf, v1FormData.startDate, v1FormData.endDate);
    Date startDate = datePair.getFirst(), endDate = datePair.getSecond();

    // Threadpool with same configs as default task threadpool.
    ThreadPoolExecutor threadpool =
        executorFactory.createExecutor("task", Executors.defaultThreadFactory());
    try {
      // Estimate only from the nodes the request targets, so the estimate matches what collection
      // will actually do. The selector returns every node when no selection was requested.
      Set<NodeDetails> selectedNodes =
          nodeSelector.resolve(universe, bundleData.nodeNames).getSelectedNodes();
      Set<NodeDetails> reachableNodes = getReachableNodes(threadpool, universe, selectedNodes);

      // Submit tasks to the threadpool to collect file size for all components in parallel.
      List<Pair<Pair<String, ComponentType>, Future<Map<String, Long>>>> futures =
          new ArrayList<>();
      for (ComponentType componentType : v1FormData.components) {
        if (componentType.getComponentLevel().equals(BundleDetails.ComponentLevel.NodeLevel)) {
          // For node level components, collect sizes from all nodes.
          for (NodeDetails node : reachableNodes) {
            SupportBundleComponent component = componentFactory.getComponent(componentType);
            Callable<Map<String, Long>> callable =
                () -> {
                  return component.getFilesListWithSizes(
                      customer, v1FormData, universe, startDate, endDate, node);
                };
            Pair<Pair<String, ComponentType>, Future<Map<String, Long>>> taskPair =
                new Pair<>(
                    new Pair<>(node.getNodeName(), componentType), threadpool.submit(callable));
            futures.add(taskPair);
          }
        } else {
          SupportBundleComponent component = componentFactory.getComponent(componentType);
          Callable<Map<String, Long>> callable =
              () -> {
                return component.getFilesListWithSizes(
                    customer, v1FormData, universe, startDate, endDate, null);
              };
          Pair<Pair<String, ComponentType>, Future<Map<String, Long>>> taskPair =
              new Pair<>(new Pair<>("YBA", componentType), threadpool.submit(callable));
          futures.add(taskPair);
        }
      }

      for (Pair<Pair<String, ComponentType>, Future<Map<String, Long>>> p : futures) {
        String nodeName = p.getFirst().getFirst();
        ComponentType component = p.getFirst().getSecond();
        try {
          Map<String, Long> componentSizeMap = p.getSecond().get();
          Long componentSize = componentSizeMap.values().stream().mapToLong(Long::longValue).sum();
          Map<String, Long> nodeComponentSizeMap = resp.getOrDefault(nodeName, new HashMap<>());
          nodeComponentSizeMap.put(component.toString(), componentSize);
          resp.put(nodeName, nodeComponentSizeMap);
        } catch (InterruptedException | ExecutionException e) {
          log.error("Error while getting file sizes for component: {}", component.toString(), e);
        }
      }
    } finally {
      threadpool.shutdown();
    }
    return new SupportBundleSizeEstimateResponse(resp);
  }

  /** PR1 stub - real estimation lands in a follow-up component PR. */
  public SupportBundleSizeEstimateResponse estimateBundleSizeYbaOnly(
      Customer customer, SupportBundleFormDataV2 bundleData) throws Exception {
    return new SupportBundleSizeEstimateResponse(new HashMap<>());
  }

  /** Check for reachable nodes among the given nodes of a universe in parallel. */
  private Set<NodeDetails> getReachableNodes(
      ThreadPoolExecutor threadpool, Universe universe, Set<NodeDetails> nodes) {
    Set<NodeDetails> reachableNodes = new HashSet<>();
    List<Pair<NodeDetails, Future<Boolean>>> futures = new ArrayList<>();
    for (NodeDetails node : nodes) {
      Callable<Boolean> callable =
          () -> {
            return nodeUniverseManager.isNodeReachable(
                node,
                universe,
                confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec));
          };
      futures.add(new Pair<NodeDetails, Future<Boolean>>(node, threadpool.submit(callable)));
    }
    // Wait for tasks to complete.
    for (Pair<NodeDetails, Future<Boolean>> p : futures) {
      try {
        if (p.getSecond().get()) {
          reachableNodes.add(p.getFirst());
        }
      } catch (InterruptedException | ExecutionException e) {
        log.error("Error while collecting reachable nodes for universe: {}", universe.getName(), e);
      }
    }
    log.info(
        "Reachable nodes for universe {} = {}",
        universe.getName(),
        reachableNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.joining(", ")));
    return reachableNodes;
  }

  /**
   * Drops the sub-components that cannot run inside a pod, and the owning component type once its
   * spec list is empty.
   */
  private void pruneK8sUnsupportedSpecs(SupportBundleFormDataV2 bundleData) {
    bundleData.filesComponentSpecs =
        pruneK8sUnsupportedSpecs(
            bundleData.components,
            ComponentType.FilesComponent,
            "filesComponentSpecs",
            bundleData.filesComponentSpecs);
    bundleData.bashComponentSpecs =
        pruneK8sUnsupportedSpecs(
            bundleData.components,
            ComponentType.BashComponent,
            "bashComponentSpecs",
            bundleData.bashComponentSpecs);
  }

  private <T extends ScriptTarComponentSpec> List<T> pruneK8sUnsupportedSpecs(
      Set<ComponentType> components,
      ComponentType componentType,
      String specsFieldName,
      List<T> specs) {
    if (CollectionUtils.isEmpty(specs)) {
      return specs;
    }
    List<T> kept =
        specs.stream()
            .filter(
                spec ->
                    spec == null
                        || SupportBundleV2K8sSupport.supportedOnK8s(spec.getComponentName()))
            .collect(Collectors.toList());
    if (kept.size() == specs.size()) {
      return specs;
    }
    String dropped =
        specs.stream()
            .filter(
                spec ->
                    spec != null
                        && !SupportBundleV2K8sSupport.supportedOnK8s(spec.getComponentName()))
            .map(ScriptTarComponentSpec::getComponentName)
            .collect(Collectors.joining(", "));
    log.warn(
        "Sub-components '{}' in '{}' are not applicable for kubernetes universes. Continuing"
            + " without them.",
        dropped,
        specsFieldName);
    if (kept.isEmpty() && components != null) {
      components.remove(componentType);
      log.warn(
          "Component '{}' has no sub-component left that applies to a kubernetes universe."
              + " Continuing without it.",
          componentType);
    }
    return kept;
  }

  /**
   * Drops {@code NODE_EXPORT} on kubernetes. {@code SwamperHelper} never writes a node_exporter
   * scrape target for a pod, so the dump would always come back empty.
   */
  private void pruneK8sUnsupportedPromExports(SupportBundleFormDataV2 bundleData) {
    if (bundleData.prometheusMetricsTypes == null
        || !bundleData.prometheusMetricsTypes.remove(PrometheusMetricsType.NODE_EXPORT)) {
      return;
    }
    log.warn(
        "Prometheus export 'NODE_EXPORT' is not applicable for kubernetes universes, where"
            + " node_exporter is not scraped. Continuing without it.");
  }

  private void validateYbaComponent(
      String componentName, String specsFieldName, List<? extends ScriptTarComponentSpec> specs) {
    validateScriptTarSpecs(componentName, specsFieldName, specs, null /* universe */);
  }

  private void validateScriptTarSpecs(
      String componentName,
      String specsFieldName,
      List<? extends ScriptTarComponentSpec> specs,
      Universe universe) {
    if (specs == null || specs.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' requires a non-empty '%s' list.", componentName, specsFieldName));
    }
    specs.forEach(
        spec -> {
          if (spec == null
              || org.apache.commons.lang3.StringUtils.isBlank(spec.getScriptPath())
              || spec.getParams() == null
              || spec.getParams().isEmpty()
              || org.apache.commons.lang3.StringUtils.isBlank(spec.getRemoteTarPath())) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Each %s spec requires 'scriptPath', non-empty 'params', and 'remoteTarPath'.",
                    componentName));
          }
          String specLabel =
              org.apache.commons.lang3.StringUtils.defaultIfBlank(
                  spec.getComponentName(), componentName);
          specValidator.validateFileNameSegment(
              componentName, "componentName", spec.getComponentName());
          specValidator.validateScriptPath(specLabel, spec.getScriptPath());
          specValidator.validateRemoteTarPath(specLabel, spec.getRemoteTarPath());
          specValidator.validateLinuxUser(specLabel, spec.getLinuxUser(), universe);
        });
  }

  private void validateNodeCommandComponent(String componentName, List<?> specs) {
    if (specs == null || specs.isEmpty()) {
      String specsField =
          componentName.equals("YbAdminComponent")
              ? "ybAdminComponentSpecs"
              : componentName.equals("YSQLComponent") ? "ysqlComponentSpecs" : "ycqlComponentSpecs";
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' requires a non-empty '%s' list.", componentName, specsField));
    }
  }
}

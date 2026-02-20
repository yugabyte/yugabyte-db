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
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.SupportBundleSizeEstimateResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
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

@Singleton
@Slf4j
public class SupportBundleHandler {

  @Inject SupportBundleUtil supportBundleUtil;
  @Inject RuntimeConfGetter confGetter;
  @Inject SupportBundleComponentFactory componentFactory;
  @Inject Config staticConf;
  @Inject PlatformExecutorFactory executorFactory;
  @Inject NodeUniverseManager nodeUniverseManager;

  /**
   * Validate support bundle form and throw BAD_REQUEST if validation fails.
   *
   * @param bundleData
   * @param universe
   */
  public void bundleDataValidation(SupportBundleFormData bundleData, Universe universe) {
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
      Customer customer, SupportBundleFormData bundleData, Universe universe) throws Exception {

    // Map to track sizes for all components.
    Map<String, Map<String, Long>> resp = new HashMap<>();
    Pair<Date, Date> datePair =
        supportBundleUtil.getValidStartAndEndDates(
            staticConf, bundleData.startDate, bundleData.endDate);
    Date startDate = datePair.getFirst(), endDate = datePair.getSecond();

    // Threadpool with same configs as default task threadpool.
    ThreadPoolExecutor threadpool =
        executorFactory.createExecutor("task", Executors.defaultThreadFactory());
    try {
      Set<NodeDetails> reachableNodes = getReachableNodes(threadpool, universe);

      // Submit tasks to the threadpool to collect file size for all components in parallel.
      List<Pair<Pair<String, ComponentType>, Future<Map<String, Long>>>> futures =
          new ArrayList<>();
      for (ComponentType componentType : bundleData.components) {
        if (componentType.getComponentLevel().equals(BundleDetails.ComponentLevel.NodeLevel)) {
          // For node level components, collect sizes from all nodes.
          for (NodeDetails node : reachableNodes) {
            SupportBundleComponent component = componentFactory.getComponent(componentType);
            Callable<Map<String, Long>> callable =
                () -> {
                  return component.getFilesListWithSizes(
                      customer, bundleData, universe, startDate, endDate, node);
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
                    customer, bundleData, universe, startDate, endDate, null);
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

  /** Check for reachable nodes in a universe in parallel. */
  private Set<NodeDetails> getReachableNodes(ThreadPoolExecutor threadpool, Universe universe) {
    Set<NodeDetails> reachableNodes = new HashSet<>();
    List<Pair<NodeDetails, Future<Boolean>>> futures = new ArrayList<>();
    for (NodeDetails node : universe.getNodes()) {
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
}

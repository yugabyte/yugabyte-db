// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.common.base.Suppliers;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.forms.SupportBundleSizeEstimateResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/** Orchestrates v2 support bundle size estimation by delegating to bucket components. */
@Singleton
@Slf4j
public class SupportBundleV2SizeEstimator {

  private final SupportBundleUtil supportBundleUtil;
  private final RuntimeConfGetter confGetter;
  private final SupportBundleComponentFactory componentFactory;
  private final Config staticConf;
  private final PlatformExecutorFactory executorFactory;
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleV2NodeSelector nodeSelector;
  private final SupportBundleClusterScope clusterScope;
  private final SupportBundleV2ManifestLoader manifestLoader;

  @Inject
  public SupportBundleV2SizeEstimator(
      SupportBundleUtil supportBundleUtil,
      RuntimeConfGetter confGetter,
      SupportBundleComponentFactory componentFactory,
      Config staticConf,
      PlatformExecutorFactory executorFactory,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleV2NodeSelector nodeSelector,
      SupportBundleClusterScope clusterScope,
      SupportBundleV2ManifestLoader manifestLoader) {
    this.supportBundleUtil = supportBundleUtil;
    this.confGetter = confGetter;
    this.componentFactory = componentFactory;
    this.staticConf = staticConf;
    this.executorFactory = executorFactory;
    this.nodeUniverseManager = nodeUniverseManager;
    this.nodeSelector = nodeSelector;
    this.clusterScope = clusterScope;
    this.manifestLoader = manifestLoader;
  }

  /**
   * Estimates a bundle's size, keyed by node then by component, so the caller can show a breakdown
   * rather than one number.
   *
   * <p>Components are scheduled onto a thread pool and joined at the end. This is latency-bound
   * work -- most of each estimate is an SSH round trip -- and a serial pass over every component on
   * every node would take minutes on a large universe for an answer meant to precede a click.
   *
   * <p>How a component is estimated depends on its shape, which is what the switch below selects:
   * per-node, once per universe with a representative node, on the YBA host, or delegated to the v1
   * estimator for the globals {@link SupportBundleV2ManifestLoader#isV1GlobalComponent} accepts.
   */
  public SupportBundleSizeEstimateResponse estimate(
      Customer customer, SupportBundleFormDataV2 bundleData, Universe universe) throws Exception {
    SupportBundleFormData v1FormData =
        SupportBundleFormDataV2Mapper.INSTANCE.toV1FormData(bundleData);
    // The per-component specs live on BundleDetails, not on the V1 form, so build the same
    // BundleDetails the collection task would persist and hand it to the components.
    BundleDetails bundleDetails = SupportBundleV2.buildBundleDetails(bundleData, confGetter);
    Pair<Date, Date> datePair =
        supportBundleUtil.getValidStartAndEndDates(
            staticConf, v1FormData.startDate, v1FormData.endDate);
    Date startDate = datePair.getFirst();
    Date endDate = datePair.getSecond();

    ThreadPoolExecutor threadpool =
        executorFactory.createExecutor("task", Executors.defaultThreadFactory());
    Map<Future<?>, String> futures = new LinkedHashMap<>();
    try {
      if (bundleData.components == null) {
        return new SupportBundleSizeEstimateResponse(new HashMap<>());
      }

      Set<NodeDetails> reachableNodes =
          getReachableNodes(
              threadpool,
              universe,
              nodeSelector.resolve(universe, bundleData.nodeNames).getSelectedNodes());
      Map<String, Map<String, Long>> resp = new ConcurrentHashMap<>();
      // Only a v1 global needs the manifest, so it isn't read otherwise.
      Supplier<SupportBundleV2Manifest.Spec> manifest =
          Suppliers.memoize(() -> manifestLoader.load().getSpec());

      for (ComponentType componentType : bundleData.components) {
        switch (componentType) {
          case FilesComponent:
          case BashComponent:
            schedulePerNodeSubComponentEstimates(
                futures,
                threadpool,
                resp,
                customer,
                v1FormData,
                bundleDetails,
                universe,
                reachableNodes,
                startDate,
                endDate,
                componentType);
            break;
          case YSQLComponent:
          case YCQLComponent:
          case YbAdminComponent:
            scheduleScopedSubComponentEstimates(
                futures,
                threadpool,
                resp,
                customer,
                v1FormData,
                bundleDetails,
                universe,
                reachableNodes,
                startDate,
                endDate,
                componentType);
            break;
          case YBAComponent:
            scheduleYbaSubComponentEstimates(
                futures,
                threadpool,
                resp,
                customer,
                v1FormData,
                bundleDetails,
                universe,
                startDate,
                endDate);
            break;
          default:
            if (SupportBundleV2ManifestLoader.isV1GlobalComponent(
                manifest.get(), componentType.name())) {
              scheduleV1GlobalEstimate(
                  futures,
                  threadpool,
                  resp,
                  customer,
                  v1FormData,
                  bundleDetails,
                  universe,
                  startDate,
                  endDate,
                  componentType);
            }
            break;
        }
      }

      waitForFutures(futures);
      return new SupportBundleSizeEstimateResponse(new HashMap<>(resp));
    } finally {
      cancelUnfinished(futures, threadpool);
    }
  }

  /** The no-universe case: only YBA-host components exist, so no node probing happens at all. */
  public SupportBundleSizeEstimateResponse estimateYbaOnly(
      Customer customer, SupportBundleFormDataV2 bundleData) throws Exception {
    SupportBundleFormData v1FormData =
        SupportBundleFormDataV2Mapper.INSTANCE.toV1FormData(bundleData);
    // The per-component specs live on BundleDetails, not on the V1 form, so build the same
    // BundleDetails the collection task would persist and hand it to the components.
    BundleDetails bundleDetails = SupportBundleV2.buildBundleDetails(bundleData, confGetter);
    Pair<Date, Date> datePair =
        supportBundleUtil.getValidStartAndEndDates(
            staticConf, v1FormData.startDate, v1FormData.endDate);
    Date startDate = datePair.getFirst();
    Date endDate = datePair.getSecond();

    Map<String, Map<String, Long>> resp = new ConcurrentHashMap<>();
    Map<Future<?>, String> futures = new LinkedHashMap<>();
    ThreadPoolExecutor threadpool =
        executorFactory.createExecutor("task", Executors.defaultThreadFactory());
    try {
      if (bundleData.components != null
          && bundleData.components.contains(ComponentType.YBAComponent)) {
        scheduleYbaSubComponentEstimates(
            futures,
            threadpool,
            resp,
            customer,
            v1FormData,
            bundleDetails,
            null,
            startDate,
            endDate);
      }
      waitForFutures(futures);
      return new SupportBundleSizeEstimateResponse(new HashMap<>(resp));
    } finally {
      cancelUnfinished(futures, threadpool);
    }
  }

  /**
   * For components whose size genuinely differs per node -- logs, cores, metadata. One task per
   * node per component, each reported under its own node key.
   */
  private void schedulePerNodeSubComponentEstimates(
      Map<Future<?>, String> futures,
      ThreadPoolExecutor threadpool,
      Map<String, Map<String, Long>> resp,
      Customer customer,
      SupportBundleFormData v1FormData,
      BundleDetails bundleDetails,
      Universe universe,
      Set<NodeDetails> reachableNodes,
      Date startDate,
      Date endDate,
      ComponentType componentType) {
    SupportBundleComponent component = componentFactory.getComponent(componentType);
    for (NodeDetails node : reachableNodes) {
      futures.put(
          threadpool.submit(
              () -> {
                try {
                  Map<String, Long> sizes =
                      component.getFilesListWithSizes(
                          customer, v1FormData, bundleDetails, universe, startDate, endDate, node);
                  mergeSubComponentSizes(resp, node.getNodeName(), sizes);
                } catch (Exception e) {
                  log.error(
                      "Error estimating {} on node '{}'", componentType, node.getNodeName(), e);
                }
              }),
          componentType + " on " + node.getNodeName());
    }
  }

  /**
   * A YSQL, YCQL or yb-admin member costs the same on every node, so one node is probed: the one a
   * cluster-scope member runs on. Node-scope members are then counted on every node the component
   * runs on and cluster-scope members once, matching what collection runs.
   */
  private void scheduleScopedSubComponentEstimates(
      Map<Future<?>, String> futures,
      ThreadPoolExecutor threadpool,
      Map<String, Map<String, Long>> resp,
      Customer customer,
      SupportBundleFormData v1FormData,
      BundleDetails bundleDetails,
      Universe universe,
      Set<NodeDetails> reachableNodes,
      Date startDate,
      Date endDate,
      ComponentType componentType) {
    NodeDetails probeNode = SupportBundleClusterScope.chooseNode(reachableNodes, componentType);
    if (probeNode == null) {
      log.warn(
          "No reachable node applies to {} on universe '{}'; skipping its estimate.",
          componentType,
          universe.getName());
      return;
    }
    List<String> runNodes =
        reachableNodes.stream()
            .filter(node -> SupportBundleClusterScope.componentRunsOn(node, componentType))
            .map(NodeDetails::getNodeName)
            .collect(Collectors.toList());
    Set<String> clusterMembers = clusterScope.clusterMembers();
    SupportBundleComponent component = componentFactory.getComponent(componentType);
    futures.put(
        threadpool.submit(
            () -> {
              try {
                Map<String, Long> sizes =
                    component.getFilesListWithSizes(
                        customer,
                        v1FormData,
                        bundleDetails,
                        universe,
                        startDate,
                        endDate,
                        probeNode);
                if (sizes == null) {
                  return;
                }
                for (Map.Entry<String, Long> entry : sizes.entrySet()) {
                  List<String> nodes =
                      clusterMembers.contains(entry.getKey())
                          ? List.of(probeNode.getNodeName())
                          : runNodes;
                  nodes.forEach(node -> putSize(resp, node, entry.getKey(), entry.getValue()));
                }
              } catch (Exception e) {
                log.error("Error estimating {} on universe", componentType, e);
              }
            }),
        componentType + " on " + probeNode.getNodeName());
  }

  private void scheduleYbaSubComponentEstimates(
      Map<Future<?>, String> futures,
      ThreadPoolExecutor threadpool,
      Map<String, Map<String, Long>> resp,
      Customer customer,
      SupportBundleFormData v1FormData,
      BundleDetails bundleDetails,
      Universe universe,
      Date startDate,
      Date endDate) {
    SupportBundleComponent component = componentFactory.getComponent(ComponentType.YBAComponent);
    futures.put(
        threadpool.submit(
            () -> {
              try {
                Map<String, Long> sizes =
                    component.getFilesListWithSizes(
                        customer, v1FormData, bundleDetails, universe, startDate, endDate, null);
                mergeSubComponentSizes(resp, SupportBundleV2EstimateRegistry.YBA_NODE_KEY, sizes);
              } catch (Exception e) {
                log.error("Error estimating YBAComponent", e);
              }
            }),
        "YBAComponent");
  }

  /**
   * Global components that already had a working v1 estimator. Reused rather than reimplemented, so
   * the v2 number for these matches what v1 reports.
   */
  private void scheduleV1GlobalEstimate(
      Map<Future<?>, String> futures,
      ThreadPoolExecutor threadpool,
      Map<String, Map<String, Long>> resp,
      Customer customer,
      SupportBundleFormData v1FormData,
      BundleDetails bundleDetails,
      Universe universe,
      Date startDate,
      Date endDate,
      ComponentType componentType) {
    futures.put(
        threadpool.submit(
            () -> {
              try {
                SupportBundleComponent component = componentFactory.getComponent(componentType);
                Map<String, Long> sizeMap =
                    component.getFilesListWithSizes(
                        customer, v1FormData, bundleDetails, universe, startDate, endDate, null);
                long total = sizeMap.values().stream().mapToLong(Long::longValue).sum();
                putSize(
                    resp,
                    SupportBundleV2EstimateRegistry.YBA_NODE_KEY,
                    componentType.name(),
                    total);
              } catch (Exception e) {
                log.error("Error estimating v1 global component {}", componentType, e);
              }
            }),
        componentType.name());
  }

  private void mergeSubComponentSizes(
      Map<String, Map<String, Long>> resp, String nodeKey, Map<String, Long> subComponentSizes) {
    if (subComponentSizes == null || subComponentSizes.isEmpty()) {
      return;
    }
    for (Map.Entry<String, Long> entry : subComponentSizes.entrySet()) {
      putSize(resp, nodeKey, entry.getKey(), entry.getValue());
    }
  }

  private void putSize(
      Map<String, Map<String, Long>> resp, String nodeKey, String componentName, long size) {
    resp.computeIfAbsent(nodeKey, ignored -> new ConcurrentHashMap<>()).put(componentName, size);
  }

  /**
   * Joins every task, logging failures instead of propagating them. One component failing to
   * estimate should cost that component's number, not the whole breakdown. No overall deadline:
   * each estimate script has its own timeout, and a partial result would read as a smaller bundle.
   * {@link InterruptedException} is propagated, since that means the request itself is going away.
   */
  private void waitForFutures(Map<Future<?>, String> futures) throws InterruptedException {
    for (Map.Entry<Future<?>, String> entry : futures.entrySet()) {
      try {
        entry.getKey().get();
      } catch (ExecutionException e) {
        log.error("Error while estimating {}", entry.getValue(), e.getCause());
      }
    }
  }

  /**
   * For an interrupted wait; otherwise every task has finished. shutdown() alone lets running
   * estimates, and the scripts behind them, carry on.
   */
  private static void cancelUnfinished(
      Map<Future<?>, String> futures, ThreadPoolExecutor threadpool) {
    futures.keySet().forEach(future -> future.cancel(true));
    threadpool.shutdownNow();
  }

  /**
   * Probes only {@code candidateNodes} so that an estimate for a node selected request covers the
   * same nodes the collection task would visit. Each probe is bounded by {@code
   * node_check_timeout_sec}, and every answer is waited for: a node whose probe queued behind
   * unreachable ones is still estimated.
   */
  private Set<NodeDetails> getReachableNodes(
      ThreadPoolExecutor threadpool, Universe universe, Collection<NodeDetails> candidateNodes)
      throws InterruptedException {
    // Insertion ordered so that picking a representative node is repeatable across runs.
    Set<NodeDetails> reachableNodes = new LinkedHashSet<>();
    if (candidateNodes.isEmpty()) {
      return reachableNodes;
    }
    long probeTimeoutSecs =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec);
    List<Pair<NodeDetails, Future<Boolean>>> futures = new ArrayList<>();
    for (NodeDetails node : candidateNodes) {
      Callable<Boolean> callable =
          () -> nodeUniverseManager.isNodeReachable(node, universe, probeTimeoutSecs);
      futures.add(new Pair<>(node, threadpool.submit(callable)));
    }
    for (Pair<NodeDetails, Future<Boolean>> pair : futures) {
      try {
        if (pair.getSecond().get()) {
          reachableNodes.add(pair.getFirst());
        }
      } catch (ExecutionException e) {
        log.error("Error while collecting reachable nodes for universe: {}", universe.getName(), e);
      }
      // An interrupt propagates: the request is going away, as in waitForFutures.
    }
    log.info(
        "Reachable nodes for universe {} = {}",
        universe.getName(),
        reachableNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.joining(", ")));
    return reachableNodes;
  }
}

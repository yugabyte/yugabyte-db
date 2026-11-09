/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks;

import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.diagnostics.SupportBundlePublisher;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder.ComponentDownloadRequest;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder.SubTaskGroupRegistry;
import com.yugabyte.yw.common.supportbundle.SupportBundleTaskTiming;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleClusterScope;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleTaskParamsV2Mapper;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleV2NodeSelector;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleServerConfRedactionSupport;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Date;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
public class CreateSupportBundleV2 extends AbstractTaskBase {

  @Inject private SupportBundleUtil supportBundleUtil;
  @Inject private Config staticConfig;
  @Inject private SupportBundlePublisher supportBundlePublisher;
  @Inject private SupportBundleV2NodeSelector nodeSelector;
  @Inject private SupportBundleSubTaskBuilder subTaskBuilder;
  @Inject private SupportBundleCgroupScriptExecutor cgroupScriptExecutor;
  @Inject private SupportBundleClusterScope clusterScope;
  @Inject private SupportBundleServerConfRedactionSupport serverConfRedactionSupport;

  /** Hands the shared builder the subtask group plumbing it cannot reach on its own. */
  private final SubTaskGroupRegistry subTaskGroupRegistry =
      new SubTaskGroupRegistry() {
        @Override
        public SubTaskGroup newGroup(String name) {
          return createSubTaskGroup(name);
        }

        @Override
        public SubTaskGroup newGroup(String name, SubTaskGroupType groupType) {
          return createSubTaskGroup(name, groupType);
        }

        @Override
        public void addGroup(SubTaskGroup subTaskGroup) {
          getRunnableTask().addSubTaskGroup(subTaskGroup);
        }
      };

  @Inject
  protected CreateSupportBundleV2(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected SupportBundleTaskParamsV2 taskParams() {
    return (SupportBundleTaskParamsV2) taskParams;
  }

  @Override
  public void run() {
    SupportBundleTaskTiming timing = SupportBundleTaskTiming.start();
    SupportBundleV2 supportBundle = taskParams().supportBundle;
    try {
      Path gzipPath = generateBundle(supportBundle, timing);
      supportBundle.setPathObject(gzipPath);

      try {
        if (taskParams().universe != null) {
          if (supportBundlePublisher.publish(gzipPath.toFile(), taskParams().universe)) {
            log.info(
                "Support bundle uploaded successfully to configured destinations with universe"
                    + " context");
          }
        } else if (supportBundlePublisher.publishForCustomer(
            gzipPath.toFile(), taskParams().customer)) {
          log.info(
              "Support bundle uploaded successfully to configured destinations with customer"
                  + " context");
        }
      } catch (Exception e) {
        log.error("Failed to upload support bundle to configured destinations", e);
      }

      supportBundle.setStatus(SupportBundleV2StatusType.Success);
    } catch (Throwable t) {
      // Catch Throwable (not just Exception) so unchecked Errors like
      // ExceptionInInitializerError / UnsatisfiedLinkError / NoClassDefFoundError
      // still flip the bundle out of Running before the finally-block persists it.
      TaskInfo taskInfo = getRunnableTask().getTaskInfo();
      if (taskInfo.getTaskState().equals(TaskInfo.State.Abort)) {
        log.info("Marking support bundle with UUID: {} as aborted.", supportBundle.getBundleUUID());
        supportBundle.setStatus(SupportBundleV2StatusType.Aborted);
      } else {
        supportBundle.setStatus(SupportBundleV2StatusType.Failed);
        Throwables.throwIfUnchecked(t);
        throw new RuntimeException(t);
      }
    } finally {
      timing.logTotalCompleted(
          log,
          "v2",
          supportBundle.getBundleUUID(),
          getUserTaskUUID(),
          universeLabel(),
          supportBundle.getStatus().name());
      try {
        supportBundle.update();
      } finally {
        // After the status is saved: this visits every staged node in turn, and the bundle
        // shouldn't show Running meanwhile. It needs neither the row nor the archive.
        cgroupScriptExecutor.cleanupBundle(supportBundle.getBundleUUID(), taskParams().universe);
      }
    }
  }

  public Path generateBundle(SupportBundleV2 supportBundle) throws Exception {
    return generateBundle(supportBundle, SupportBundleTaskTiming.start());
  }

  public Path generateBundle(SupportBundleV2 supportBundle, SupportBundleTaskTiming timing)
      throws Exception {
    timing.startCollection();
    String collectionStatus = "Failed";
    Set<NodeDetails> reachableNodes = ConcurrentHashMap.newKeySet();
    try {
      Customer customer = taskParams().customer;
      Universe universe = taskParams().universe;
      boolean ybaOnly = supportBundle.isYbaOnly();
      SupportBundleV2 bundleForCollection = reloadSupportBundleForCollection(supportBundle);
      Path bundlePath = subTaskBuilder.buildBundlePath(universe);
      supportBundle.setPathObject(bundlePath);
      // Saved before the directory exists: a failed, aborted or YBA-restarted bundle keeps this
      // path, which is what SupportBundleCleanup deletes along with the row.
      supportBundle.update();
      bundleForCollection.setPathObject(bundlePath);
      Files.createDirectories(bundlePath);
      if (universe != null) {
        log.debug("Fetching Universe {} logs", universe.getName());
      } else {
        log.debug("Fetching YBA platform host support bundle components");
      }

      Pair<Date, Date> datePair =
          supportBundleUtil.getValidStartAndEndDates(
              staticConfig, supportBundle.getStartDate(), supportBundle.getEndDate());
      Date startDate = datePair.getFirst(), endDate = datePair.getSecond();

      // Add the supportBundle metadata into the bundle
      subTaskBuilder.saveManifest(customer, bundlePath, bundleForCollection);

      if (!ybaOnly) {
        int nodeReachableTimeout =
            confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec);

        // Probing only the requested nodes is the point of the node selection: on a large universe
        // a two node bundle should not pay for a reachability check on every other node. The
        // selector returns every node when no selection was persisted, keeping the default path
        // unchanged.
        Set<NodeDetails> selectedNodes =
            nodeSelector
                .resolve(universe, bundleForCollection.getBundleDetails().getNodeNames())
                .getSelectedNodes();
        if (selectedNodes.size() < universe.getNodes().size()) {
          log.info(
              "Collecting node-level components for universe '{}' from the {} selected node(s): {}",
              universe.getName(),
              selectedNodes.size(),
              SupportBundleV2NodeSelector.nodeNamesOf(selectedNodes));
        }

        // Run checks to all nodes in parallel to optimise bundle creation time.
        subTaskBuilder.addNodeReachabilityChecks(
            subTaskGroupRegistry, selectedNodes, universe, nodeReachableTimeout, reachableNodes);
        getRunnableTask().runSubTasks();

        // Clear previous subtask so its not run again.
        getRunnableTask().reset();

        if (reachableNodes.size() < selectedNodes.size()) {
          Set<NodeDetails> unreachableNodes = new LinkedHashSet<>(selectedNodes);
          unreachableNodes.removeAll(reachableNodes);
          if (reachableNodes.isEmpty()) {
            log.warn(
                "No reachable DB nodes for universe '{}': node-level components (e.g."
                    + " collect_threadz_memz_rpcz_stats, collect_heap_snapshot,"
                    + " collect_cdc_state) will not be collected."
                    + " Verify node-agent or SSH connectivity from YBA to universe nodes.",
                universe.getName());
          } else {
            log.warn(
                "Skipping node-level components for unreachable node(s) {} of universe '{}'.",
                SupportBundleV2NodeSelector.nodeNamesOf(unreachableNodes),
                universe.getName());
          }
        }
        clusterScope.register(supportBundle.getBundleUUID(), reachableNodes);
      }

      // Create new subtask groups, one for each component.
      subTaskBuilder.addComponentDownloadTasks(
          subTaskGroupRegistry,
          ComponentDownloadRequest.builder()
              .bundleDetails(bundleForCollection.getBundleDetails())
              .v1TaskParams(SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(taskParams()))
              .customer(customer)
              .universe(universe)
              .reachableNodes(reachableNodes)
              .startDate(startDate)
              .endDate(endDate)
              .bundlePath(bundlePath)
              .ybaOnly(ybaOnly)
              .build());
      getRunnableTask().runSubTasks();

      // Tar the support bundle directory and delete the original folder
      Path gzipPath = Util.zipAndDeleteDir(bundlePath);
      log.debug(
          "Finished aggregating logs for support bundle with UUID {}",
          supportBundle.getBundleUUID());
      collectionStatus = "Success";
      return gzipPath;
    } finally {
      SupportBundleStaging.deleteFor(supportBundle.getBundleUUID());
      clusterScope.forgetBundle(supportBundle.getBundleUUID());
      serverConfRedactionSupport.forgetBundle(supportBundle.getBundleUUID());
      timing.logCollectionCompleted(
          log,
          "v2",
          supportBundle.getBundleUUID(),
          getUserTaskUUID(),
          universeLabel(),
          collectionStatus);
    }
  }

  private String universeLabel() {
    Universe universe = taskParams().universe;
    return universe != null ? universe.getName() : "yba-only";
  }

  /** Reload persisted bundle details and merge spec lists into task params for collection. */
  private SupportBundleV2 reloadSupportBundleForCollection(SupportBundleV2 supportBundle) {
    SupportBundleV2 dbBundle = SupportBundleV2.get(supportBundle.getBundleUUID());
    SupportBundleV2 bundleForCollection = dbBundle != null ? dbBundle : supportBundle;
    hydrateTaskBundleDataFromDetails(bundleForCollection.getBundleDetails());
    taskParams().supportBundle = bundleForCollection;
    return bundleForCollection;
  }

  private void hydrateTaskBundleDataFromDetails(BundleDetails details) {
    if (details == null) {
      return;
    }
    if (taskParams().bundleData == null) {
      taskParams().bundleData = new SupportBundleFormDataV2();
    }
    SupportBundleFormDataV2 data = taskParams().bundleData;
    if (data.components == null || data.components.isEmpty()) {
      data.components = details.getComponents();
    }
    if (data.bashComponentSpecs == null || data.bashComponentSpecs.isEmpty()) {
      data.bashComponentSpecs = details.getBashComponentSpecs();
    }
    if (data.filesComponentSpecs == null || data.filesComponentSpecs.isEmpty()) {
      data.filesComponentSpecs = details.getFilesComponentSpecs();
    }
    if (data.ysqlComponentSpecs == null || data.ysqlComponentSpecs.isEmpty()) {
      data.ysqlComponentSpecs = details.getYsqlComponentSpecs();
    }
    if (data.ycqlComponentSpecs == null || data.ycqlComponentSpecs.isEmpty()) {
      data.ycqlComponentSpecs = details.getYcqlComponentSpecs();
    }
    if (data.ybAdminComponentSpecs == null || data.ybAdminComponentSpecs.isEmpty()) {
      data.ybAdminComponentSpecs = details.getYbAdminComponentSpecs();
    }
    if (data.ybaComponentSpecs == null || data.ybaComponentSpecs.isEmpty()) {
      data.ybaComponentSpecs = details.getYbaComponentSpecs();
    }
  }
}

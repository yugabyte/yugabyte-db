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
import com.yugabyte.yw.common.supportbundle.SupportBundleTaskParamsV2Mapper;
import com.yugabyte.yw.common.supportbundle.SupportBundleV2NodeSelector;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
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
    SupportBundleV2 supportBundle = taskParams().supportBundle;
    try {
      Path gzipPath = generateBundle(supportBundle);
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
      supportBundle.update();
    }
  }

  public Path generateBundle(SupportBundleV2 supportBundle) throws Exception {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    boolean ybaOnly = supportBundle.isYbaOnly();
    Path bundlePath = subTaskBuilder.buildBundlePath(universe);
    supportBundle.setPathObject(bundlePath);
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
    subTaskBuilder.saveManifest(customer, bundlePath, supportBundle);

    Set<NodeDetails> reachableNodes = ConcurrentHashMap.newKeySet();
    if (!ybaOnly) {
      int nodeReachableTimeout =
          confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec);

      // Probing only the requested nodes is the point of the node selection: on a large universe
      // a two node bundle should not pay for a reachability check on every other node. The
      // selector returns every node when no selection was persisted, keeping the default path
      // unchanged.
      Set<NodeDetails> selectedNodes =
          nodeSelector
              .resolve(universe, supportBundle.getBundleDetails().getNodeNames())
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

      if (!reachableNodes.isEmpty() && reachableNodes.size() < selectedNodes.size()) {
        Set<NodeDetails> unreachableNodes = new LinkedHashSet<>(selectedNodes);
        unreachableNodes.removeAll(reachableNodes);
        log.warn(
            "Skipping node-level components for unreachable node(s) {} of universe '{}'.",
            SupportBundleV2NodeSelector.nodeNamesOf(unreachableNodes),
            universe.getName());
      }
    }

    // Create new subtask groups, one for each component.
    subTaskBuilder.addComponentDownloadTasks(
        subTaskGroupRegistry,
        ComponentDownloadRequest.builder()
            .bundleDetails(supportBundle.getBundleDetails())
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
        "Finished aggregating logs for support bundle with UUID {}", supportBundle.getBundleUUID());
    return gzipPath;
  }
}

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
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.diagnostics.SupportBundlePublisher;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder.ComponentDownloadRequest;
import com.yugabyte.yw.common.supportbundle.SupportBundleSubTaskBuilder.SubTaskGroupRegistry;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Date;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
public class CreateSupportBundle extends AbstractTaskBase {

  @Inject private SupportBundleUtil supportBundleUtil;
  @Inject private Config staticConfig;
  @Inject private SupportBundlePublisher supportBundlePublisher;
  @Inject private SupportBundleSubTaskBuilder subTaskBuilder;

  private final OperatorStatusUpdater operatorStatusUpdater;

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
  protected CreateSupportBundle(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies);
    this.operatorStatusUpdater = operatorStatusUpdaterFactory.create();
  }

  @Override
  protected SupportBundleTaskParams taskParams() {
    return (SupportBundleTaskParams) taskParams;
  }

  @Override
  public void run() {
    SupportBundle supportBundle = taskParams().supportBundle;
    try {
      Path gzipPath = generateBundle(supportBundle);
      supportBundle.setPathObject(gzipPath);

      // Upload support bundle to configured destinations with universe context
      try {
        if (supportBundlePublisher.publish(gzipPath.toFile(), taskParams().universe)) {
          log.info(
              "Support bundle uploaded successfully to configured destinations with universe"
                  + " context");
        }
      } catch (Exception e) {
        log.error("Failed to upload support bundle to configured destinations", e);
        // Don't fail the entire task if upload fails - the bundle was created successfully
      }

      supportBundle.setStatus(SupportBundleStatusType.Success);
      operatorStatusUpdater.markSupportBundleFinished(
          supportBundle, taskParams().getKubernetesResourceDetails(), gzipPath);
    } catch (Throwable t) {
      // Catch Throwable (not just Exception) so unchecked Errors like
      // ExceptionInInitializerError / UnsatisfiedLinkError / NoClassDefFoundError
      // still flip the bundle out of Running before the finally-block persists it.
      TaskInfo taskInfo = getRunnableTask().getTaskInfo();
      if (taskInfo.getTaskState().equals(TaskInfo.State.Abort)) {
        log.info("Marking support bundle with UUID: {} as aborted.", supportBundle.getBundleUUID());
        supportBundle.setStatus(SupportBundleStatusType.Aborted);
      } else {
        supportBundle.setStatus(SupportBundleStatusType.Failed);
        operatorStatusUpdater.markSupportBundleFailed(
            supportBundle, taskParams().getKubernetesResourceDetails());
        Throwables.throwIfUnchecked(t);
        throw new RuntimeException(t);
      }
    } finally {
      supportBundle.update();
    }
  }

  public Path generateBundle(SupportBundle supportBundle) throws Exception {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    Path bundlePath = subTaskBuilder.buildBundlePath(universe);
    supportBundle.setPathObject(bundlePath);
    Files.createDirectories(bundlePath);
    log.debug("Fetching Universe {} logs", universe.getName());

    Pair<Date, Date> datePair =
        supportBundleUtil.getValidStartAndEndDates(
            staticConfig, supportBundle.getStartDate(), supportBundle.getEndDate());
    Date startDate = datePair.getFirst(), endDate = datePair.getSecond();

    // Add the supportBundle metadata into the bundle
    subTaskBuilder.saveManifest(customer, bundlePath, supportBundle);

    int nodeReachableTimeout =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec);
    Set<NodeDetails> reachableNodes = ConcurrentHashMap.newKeySet();

    // Run checks to all nodes in parallel to optimise bundle creation time.
    subTaskBuilder.addNodeReachabilityChecks(
        subTaskGroupRegistry, universe.getNodes(), universe, nodeReachableTimeout, reachableNodes);
    getRunnableTask().runSubTasks();

    // Clear previous subtask so its not run again.
    getRunnableTask().reset();

    // Create new subtask groups, one for each component.
    subTaskBuilder.addComponentDownloadTasks(
        subTaskGroupRegistry,
        ComponentDownloadRequest.builder()
            .bundleDetails(supportBundle.getBundleDetails())
            .v1TaskParams(taskParams())
            .customer(customer)
            .universe(universe)
            .reachableNodes(reachableNodes)
            .startDate(startDate)
            .endDate(endDate)
            .bundlePath(bundlePath)
            .ybaOnly(false)
            .build());
    getRunnableTask().runSubTasks();

    // Tar the support bundle directory and delete the original folder
    Path gzipPath = Util.zipAndDeleteDir(bundlePath);
    log.debug(
        "Finished aggregating logs for support bundle with UUID {}", supportBundle.getBundleUUID());
    return gzipPath;
  }
}

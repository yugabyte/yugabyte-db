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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckNodeReachable;
import com.yugabyte.yw.commissioner.tasks.subtasks.SupportBundleComponentDownload;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.diagnostics.SupportBundlePublisher;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.util.Collection;
import java.util.Date;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Abortable
public class CreateSupportBundle extends AbstractTaskBase {

  @Inject private SupportBundleComponentFactory supportBundleComponentFactory;
  @Inject private SupportBundleUtil supportBundleUtil;
  @Inject private Config staticConfig;
  @Inject private SupportBundlePublisher supportBundlePublisher;

  private final OperatorStatusUpdater operatorStatusUpdater;

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
    } catch (Exception e) {
      TaskInfo taskInfo = getRunnableTask().getTaskInfo();
      if (taskInfo.getTaskState().equals(TaskInfo.State.Abort)) {
        log.info("Marking support bundle with UUID: {} as aborted.", supportBundle.getBundleUUID());
        supportBundle.setStatus(SupportBundleStatusType.Aborted);
      } else {
        supportBundle.setStatus(SupportBundleStatusType.Failed);
        operatorStatusUpdater.markSupportBundleFailed(
            supportBundle, taskParams().getKubernetesResourceDetails());
        Throwables.throwIfUnchecked(e);
        throw new RuntimeException(e);
      }
    } finally {
      supportBundle.update();
    }
  }

  public Path generateBundle(SupportBundle supportBundle) throws Exception {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    Path bundlePath = generateBundlePath(universe);
    supportBundle.setPathObject(bundlePath);
    Files.createDirectories(bundlePath);
    log.debug("Fetching Universe {} logs", universe.getName());

    Pair<Date, Date> datePair =
        supportBundleUtil.getValidStartAndEndDates(
            staticConfig, supportBundle.getStartDate(), supportBundle.getEndDate());
    Date startDate = datePair.getFirst(), endDate = datePair.getSecond();

    // Add the supportBundle metadata into the bundle
    try {
      JsonNode sbJson = Json.toJson(supportBundle);
      ((ObjectNode) sbJson).remove("status");
      ((ObjectNode) sbJson).remove("sizeInBytes");
      supportBundleUtil.saveMetadata(
          customer, bundlePath.toAbsolutePath().toString(), sbJson, "manifest.json");
    } catch (Exception e) {
      // Log the error and continue with the rest of support bundle collection.
      log.error("Error occurred while collecting support bundle manifest json", e);
    }

    // Filters out the nodes which are unresponsive for 60 seconds or throw error for simple ssh
    // command like ls. We do not collect any of the components from these nodes. This is mainly
    // done to help optimise the speed of support bundles when a node(s) is down.
    int nodeReachableTimeout =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec);
    Set<NodeDetails> reachableNodes = ConcurrentHashMap.newKeySet();

    // Run checks to all nodes in parallel to optimise bundle creation time.
    createCheckNodeReachableSubTask(
        universe.getNodes(), universe, nodeReachableTimeout, reachableNodes);
    getRunnableTask().runSubTasks();

    // Clear previous subtask so its not run again.
    getRunnableTask().reset();

    // Create new subtask groups, one for each component.
    createSupportBundleComponentDownloadTasks(
        supportBundle, reachableNodes, customer, universe, startDate, endDate, bundlePath);
    getRunnableTask().runSubTasks();

    // Tar the support bundle directory and delete the original folder
    Path gzipPath = Util.zipAndDeleteDir(bundlePath);
    log.debug(
        "Finished aggregating logs for support bundle with UUID {}", supportBundle.getBundleUUID());
    return gzipPath;
  }

  private Path generateBundlePath(Universe universe) {
    String storagePath = AppConfigHelper.getStoragePath();
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundleName = "yb-support-bundle-" + universe.getName() + "-" + datePrefix + "-logs";
    return Paths.get(storagePath + "/" + bundleName);
  }

  private void createCheckNodeReachableSubTask(
      Collection<NodeDetails> nodes,
      Universe universe,
      long nodeReachableTimeout,
      Set<NodeDetails> reachableNodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckNodeReachable");
    for (NodeDetails node : nodes) {
      CheckNodeReachable.Params params = new CheckNodeReachable.Params();
      params.node = node;
      params.universe = universe;
      params.nodeReachableTimeout = nodeReachableTimeout;
      params.nodesReachable = reachableNodes;

      CheckNodeReachable CheckNodeReachable = createTask(CheckNodeReachable.class);
      CheckNodeReachable.initialize(params);
      subTaskGroup.addSubTask(CheckNodeReachable);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  private void createSupportBundleComponentDownloadTasks(
      SupportBundle supportBundle,
      Set<NodeDetails> reachableNodes,
      Customer customer,
      Universe universe,
      Date startDate,
      Date endDate,
      Path bundlePath) {
    for (BundleDetails.ComponentType componentType :
        supportBundle.getBundleDetails().getComponents()) {
      // Will be done at the end.
      if (componentType.equals(BundleDetails.ComponentType.ApplicationLogs)) {
        continue;
      }
      SubTaskGroup subTaskGroup =
          createSubTaskGroup(
              componentType.name() + "ComponentDownload",
              SubTaskGroupType.SupportBundleComponentDownload);
      final SupportBundleTaskParams taskParams = taskParams();
      SupportBundleComponent supportBundleComponent =
          supportBundleComponentFactory.getComponent(componentType);
      if (componentType.getComponentLevel().equals(BundleDetails.ComponentLevel.NodeLevel)) {
        // For Node level component, add subtasks for all nodes to the same group.
        for (NodeDetails node : reachableNodes) {
          try {
            Path dirPath = Paths.get(bundlePath.toAbsolutePath().toString(), node.nodeName);
            SupportBundleComponentDownload.Params params =
                new SupportBundleComponentDownload.Params(
                    supportBundleComponent,
                    taskParams,
                    customer,
                    universe,
                    dirPath,
                    node,
                    startDate,
                    endDate);
            Files.createDirectories(dirPath);
            SupportBundleComponentDownload downloadTask =
                createTask(SupportBundleComponentDownload.class);
            downloadTask.initialize(params);
            subTaskGroup.addSubTask(downloadTask);
          } catch (Exception e) {
            log.error(
                "Error while creating {} component download task for node {}",
                componentType,
                node.getNodeName(),
                e);
          }
        }
      } else {
        try {
          Path dirPath = Paths.get(bundlePath.toAbsolutePath().toString(), "YBA");
          SupportBundleComponentDownload.Params params =
              new SupportBundleComponentDownload.Params(
                  supportBundleComponent,
                  taskParams,
                  customer,
                  universe,
                  dirPath,
                  /* node */ null,
                  startDate,
                  endDate);
          Files.createDirectories(dirPath);
          SupportBundleComponentDownload downloadTask =
              createTask(SupportBundleComponentDownload.class);
          downloadTask.initialize(params);
          subTaskGroup.addSubTask(downloadTask);
        } catch (Exception e) {
          log.error(
              "Error while creating {} component download task for YBA node", componentType, e);
        }
      }
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    // Collect application logs
    if (supportBundle
        .getBundleDetails()
        .getComponents()
        .contains(BundleDetails.ComponentType.ApplicationLogs)) {
      try {
        SubTaskGroup subTaskGroup =
            createSubTaskGroup(
                "ApplicationLogsComponentDownload",
                SubTaskGroupType.SupportBundleComponentDownload);
        SupportBundleComponent supportBundleComponent =
            supportBundleComponentFactory.getComponent(BundleDetails.ComponentType.ApplicationLogs);
        Path dirPath = Paths.get(bundlePath.toAbsolutePath().toString(), "YBA");
        SupportBundleComponentDownload.Params params =
            new SupportBundleComponentDownload.Params(
                supportBundleComponent,
                taskParams(),
                customer,
                universe,
                dirPath,
                /* node */ null,
                startDate,
                endDate);
        Files.createDirectories(dirPath);
        SupportBundleComponentDownload downloadTask =
            createTask(SupportBundleComponentDownload.class);
        downloadTask.initialize(params);
        subTaskGroup.addSubTask(downloadTask);
        getRunnableTask().addSubTaskGroup(subTaskGroup);
      } catch (Exception e) {
        log.error("Error while collecting Application logs for support bundle", e);
      }
    }
  }
}

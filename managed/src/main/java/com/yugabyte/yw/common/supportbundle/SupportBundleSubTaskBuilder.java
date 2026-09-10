// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckNodeReachable;
import com.yugabyte.yw.commissioner.tasks.subtasks.SupportBundleComponentDownload;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.Customer;
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
import lombok.Builder;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/**
 * Subtask construction shared by the v1 and v2 create support bundle tasks. The two versions
 * collect components identically and differ only in which bundle model they persist, which nodes
 * they probe and where they publish. Those differences stay in the tasks; everything below them
 * lives here so a fix lands on both paths at once.
 *
 * <p>Callers speak v1 types. That costs the v2 task nothing, since {@link
 * SupportBundleTaskParamsV2Mapper#toV1TaskParams} already normalizes v2 state into them for the
 * component collection layer.
 */
@Slf4j
@Singleton
public class SupportBundleSubTaskBuilder {

  private final SupportBundleComponentFactory supportBundleComponentFactory;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public SupportBundleSubTaskBuilder(
      SupportBundleComponentFactory supportBundleComponentFactory,
      SupportBundleUtil supportBundleUtil) {
    this.supportBundleComponentFactory = supportBundleComponentFactory;
    this.supportBundleUtil = supportBundleUtil;
  }

  /**
   * Grants access to the subtask group plumbing on {@link AbstractTaskBase}, which is {@code
   * protected} and therefore unreachable from this class. Each task implements it by forwarding to
   * its own inherited members.
   */
  public interface SubTaskGroupRegistry {

    SubTaskGroup newGroup(String name);

    SubTaskGroup newGroup(String name, SubTaskGroupType groupType);

    void addGroup(SubTaskGroup subTaskGroup);
  }

  /** Everything the component download subtasks need, resolved by the caller. */
  @Value
  @Builder
  public static class ComponentDownloadRequest {
    BundleDetails bundleDetails;
    SupportBundleTaskParams v1TaskParams;
    Customer customer;
    Universe universe;
    Set<NodeDetails> reachableNodes;
    Date startDate;
    Date endDate;
    Path bundlePath;

    /** Restricts collection to the YBA component, for bundles taken without a universe. */
    boolean ybaOnly;
  }

  /** A null universe means a YBA only bundle, which is not tied to any universe name. */
  public Path buildBundlePath(Universe universe) {
    String storagePath = AppConfigHelper.getStoragePath();
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundleName =
        universe == null
            ? "yb-support-bundle-yba-" + datePrefix + "-logs"
            : "yb-support-bundle-" + universe.getName() + "-" + datePrefix + "-logs";
    return Paths.get(storagePath + "/" + bundleName);
  }

  /** Writes the bundle's own metadata into the bundle. Accepts either bundle model version. */
  public void saveManifest(Customer customer, Path bundlePath, Object supportBundle) {
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
  }

  /**
   * Filters out the nodes which are unresponsive for 60 seconds or throw error for simple ssh
   * command like ls. We do not collect any of the components from these nodes. This is mainly done
   * to help optimise the speed of support bundles when a node(s) is down.
   */
  public void addNodeReachabilityChecks(
      SubTaskGroupRegistry registry,
      Collection<NodeDetails> nodes,
      Universe universe,
      long nodeReachableTimeout,
      Set<NodeDetails> reachableNodes) {
    SubTaskGroup subTaskGroup = registry.newGroup("CheckNodeReachable");
    for (NodeDetails node : nodes) {
      CheckNodeReachable.Params params = new CheckNodeReachable.Params();
      params.node = node;
      params.universe = universe;
      params.nodeReachableTimeout = nodeReachableTimeout;
      params.nodesReachable = reachableNodes;

      CheckNodeReachable checkNodeReachable = AbstractTaskBase.createTask(CheckNodeReachable.class);
      checkNodeReachable.initialize(params);
      subTaskGroup.addSubTask(checkNodeReachable);
    }
    registry.addGroup(subTaskGroup);
  }

  /** Adds one subtask group per component, with node level components fanned out across nodes. */
  public void addComponentDownloadTasks(
      SubTaskGroupRegistry registry, ComponentDownloadRequest request) {
    Set<BundleDetails.ComponentType> componentTypes = request.getBundleDetails().getComponents();
    for (BundleDetails.ComponentType componentType : componentTypes) {
      if (request.isYbaOnly() && !componentType.equals(BundleDetails.ComponentType.YBAComponent)) {
        continue;
      }
      // Will be done at the end.
      if (componentType.equals(BundleDetails.ComponentType.ApplicationLogs)) {
        continue;
      }
      SubTaskGroup subTaskGroup =
          registry.newGroup(
              componentType.name() + "ComponentDownload",
              SubTaskGroupType.SupportBundleComponentDownload);
      SupportBundleComponent supportBundleComponent =
          supportBundleComponentFactory.getComponent(componentType);
      if (componentType.getComponentLevel().equals(BundleDetails.ComponentLevel.NodeLevel)) {
        // For Node level component, add subtasks for all nodes to the same group.
        for (NodeDetails node : request.getReachableNodes()) {
          try {
            addDownloadSubTask(
                subTaskGroup, request, supportBundleComponent, node, node.getNodeName());
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
          addDownloadSubTask(subTaskGroup, request, supportBundleComponent, null, "YBA");
        } catch (Exception e) {
          log.error(
              "Error while creating {} component download task for YBA node", componentType, e);
        }
      }
      registry.addGroup(subTaskGroup);
    }
    // Collect application logs
    if (!request.isYbaOnly()
        && componentTypes.contains(BundleDetails.ComponentType.ApplicationLogs)) {
      try {
        SubTaskGroup subTaskGroup =
            registry.newGroup(
                "ApplicationLogsComponentDownload",
                SubTaskGroupType.SupportBundleComponentDownload);
        SupportBundleComponent supportBundleComponent =
            supportBundleComponentFactory.getComponent(BundleDetails.ComponentType.ApplicationLogs);
        addDownloadSubTask(subTaskGroup, request, supportBundleComponent, null, "YBA");
        registry.addGroup(subTaskGroup);
      } catch (Exception e) {
        log.error("Error while collecting Application logs for support bundle", e);
      }
    }
  }

  private void addDownloadSubTask(
      SubTaskGroup subTaskGroup,
      ComponentDownloadRequest request,
      SupportBundleComponent supportBundleComponent,
      NodeDetails node,
      String dirName)
      throws Exception {
    Path dirPath = Paths.get(request.getBundlePath().toAbsolutePath().toString(), dirName);
    SupportBundleComponentDownload.Params params =
        new SupportBundleComponentDownload.Params(
            supportBundleComponent,
            request.getV1TaskParams(),
            request.getCustomer(),
            request.getUniverse(),
            dirPath,
            node,
            request.getStartDate(),
            request.getEndDate());
    Files.createDirectories(dirPath);
    SupportBundleComponentDownload downloadTask =
        AbstractTaskBase.createTask(SupportBundleComponentDownload.class);
    downloadTask.initialize(params);
    subTaskGroup.addSubTask(downloadTask);
  }
}

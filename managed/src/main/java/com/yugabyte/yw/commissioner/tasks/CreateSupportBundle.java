package com.yugabyte.yw.commissioner.tasks;

import com.google.common.base.Throwables;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckNodeReachable;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import java.util.zip.GZIPOutputStream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.io.FileUtils;
import play.libs.Json;

@Slf4j
public class CreateSupportBundle extends AbstractTaskBase {

  @Inject private UniverseInfoHandler universeInfoHandler;
  @Inject private SupportBundleComponentFactory supportBundleComponentFactory;
  @Inject private SupportBundleUtil supportBundleUtil;
  @Inject private Config config;
  @Inject private NodeUniverseManager nodeUniverseManager;
  @Inject RuntimeConfGetter confGetter;

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
      supportBundle.setStatus(SupportBundleStatusType.Success);
      operatorStatusUpdater.markSupportBundleFinished(
          supportBundle, taskParams().getKubernetesResourceDetails(), gzipPath);
    } catch (Exception e) {
      taskParams().supportBundle.setStatus(SupportBundleStatusType.Failed);
      operatorStatusUpdater.markSupportBundleFailed(
          supportBundle, taskParams().getKubernetesResourceDetails());
      Throwables.throwIfUnchecked(e);
      throw new RuntimeException(e);
    } finally {
      supportBundle.update();
    }
  }

  public Path generateBundle(SupportBundle supportBundle)
      throws IOException, RuntimeException, ParseException {
    Customer customer = taskParams().customer;
    Universe universe = taskParams().universe;
    Path bundlePath = generateBundlePath(universe);
    supportBundle.setPathObject(bundlePath);
    Files.createDirectories(bundlePath);
    Path gzipPath =
        Paths.get(bundlePath.toAbsolutePath().toString().concat(".tar.gz")); // test this path
    log.debug("gzip support bundle path: {}", gzipPath.toString());
    log.debug("Fetching Universe {} logs", universe.getName());

    // Simplified the following 4 cases to extract appropriate start and end date
    // 1. If both of the dates are given and valid
    // 2. If only the start date is valid, filter from startDate till the end
    // 3. If only the end date is valid, filter from the beginning till endDate
    // 4. Default : If no dates are specified, download all the files from last n days
    Date startDate, endDate;
    boolean startDateIsValid = supportBundleUtil.isValidDate(supportBundle.getStartDate());
    boolean endDateIsValid = supportBundleUtil.isValidDate(supportBundle.getEndDate());
    if (!startDateIsValid && !endDateIsValid) {
      int default_date_range = config.getInt("yb.support_bundle.default_date_range");
      endDate = supportBundleUtil.getTodaysDate();
      startDate = supportBundleUtil.getDateNDaysAgo(endDate, default_date_range);
    } else {
      startDate = startDateIsValid ? supportBundle.getStartDate() : new Date(Long.MIN_VALUE);
      endDate = endDateIsValid ? supportBundle.getEndDate() : new Date(Long.MAX_VALUE);
    }

    // add the supportBundle metadata into the bundle
    try {
      supportBundleUtil.saveMetadata(
          customer,
          bundlePath.toAbsolutePath().toString(),
          Json.toJson(supportBundle),
          "manifest.json");
    } catch (Exception e) {
      // Log the error and continue with the rest of support bundle collection.
      log.error("Error occurred while collecting support bundle manifest json", e);
    }

    // Filters out the nodes which are unresponsive for 60 seconds or throw error for simple ssh
    // command like ls. We do not collect any of the components from these nodes. This is mainly
    // done to help optimise the speed of support bundles when a node(s) is down.
    int nodeReachableTimeout =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec);
    Set<NodeDetails> nodesReachable = ConcurrentHashMap.newKeySet();
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    for (NodeDetails node : nodes) {
      createCheckNodeReachableTask(node, universe, nodeReachableTimeout, nodesReachable);
    }
    // Run checks to all nodes in parallel to optimise bundle creation time.
    getRunnableTask().runSubTasks();

    // Downloads each type of node level support bundle component type into the bundle path
    for (NodeDetails node : nodesReachable) {
      for (BundleDetails.ComponentType componentType :
          supportBundle.getBundleDetails().getNodeLevelComponents()) {
        SupportBundleComponent supportBundleComponent =
            supportBundleComponentFactory.getComponent(componentType);
        try {
          // Call the downloadComponentBetweenDates() function for all node level components with
          // the node object.
          // Each component verifies if the dates are required and calls the downloadComponent().
          Path nodeComponentsDirPath =
              Paths.get(bundlePath.toAbsolutePath().toString(), node.nodeName);
          Files.createDirectories(nodeComponentsDirPath);
          supportBundleComponent.downloadComponentBetweenDates(
              taskParams(), customer, universe, nodeComponentsDirPath, startDate, endDate, node);
        } catch (Exception e) {
          // Log the error and continue with the rest of support bundle collection.
          log.error(
              "Error occurred in node level support bundle collection for component '{} on node"
                  + " '{}''.",
              componentType,
              node.getNodeName(),
              e);
        }
      }
    }

    // Downloads each type of global level support bundle component type into the bundle path
    for (BundleDetails.ComponentType componentType :
        supportBundle.getBundleDetails().getGlobalLevelComponents()) {
      SupportBundleComponent supportBundleComponent =
          supportBundleComponentFactory.getComponent(componentType);
      try {
        // Call the downloadComponentBetweenDates() function for all global level components with
        // node = null.
        // Each component verifies if the dates are required and calls the downloadComponent().
        Path globalComponentsDirPath = Paths.get(bundlePath.toAbsolutePath().toString(), "YBA");
        Files.createDirectories(globalComponentsDirPath);
        supportBundleComponent.downloadComponentBetweenDates(
            taskParams(), customer, universe, globalComponentsDirPath, startDate, endDate, null);
      } catch (Exception e) {
        // Log the error and continue with the rest of support bundle collection.
        log.error(
            "Error occurred in global level support bundle collection for component '{}'.",
            componentType,
            e);
      }
    }

    // Tar the support bundle directory and delete the original folder
    try (FileOutputStream fos = new FileOutputStream(gzipPath.toString()); // need to test this path
        GZIPOutputStream gos = new GZIPOutputStream(new BufferedOutputStream(fos));
        TarArchiveOutputStream tarOS = new TarArchiveOutputStream(gos)) {

      tarOS.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);
      tarOS.setBigNumberMode(TarArchiveOutputStream.BIGNUMBER_POSIX);
      Util.addFilesToTarGZ(bundlePath.toString(), "", tarOS);
      FileUtils.deleteDirectory(new File(bundlePath.toAbsolutePath().toString()));
    }
    log.debug(
        "Finished aggregating logs for support bundle with UUID {}", supportBundle.getBundleUUID());
    return gzipPath;
  }

  private Path generateBundlePath(Universe universe) {
    String storagePath = AppConfigHelper.getStoragePath();
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundleName = "yb-support-bundle-" + universe.getName() + "-" + datePrefix + "-logs";
    Path bundlePath = Paths.get(storagePath + "/" + bundleName);
    return bundlePath;
  }

  protected SubTaskGroup createCheckNodeReachableTask(
      NodeDetails node,
      Universe universe,
      long nodeReachableTimeout,
      Set<NodeDetails> nodesReachable) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckNodeReachable");
    CheckNodeReachable.Params params = new CheckNodeReachable.Params();
    params.node = node;
    params.universe = universe;
    params.nodeReachableTimeout = nodeReachableTimeout;
    params.nodesReachable = nodesReachable;

    CheckNodeReachable CheckNodeReachable = createTask(CheckNodeReachable.class);
    CheckNodeReachable.initialize(params);
    subTaskGroup.addSubTask(CheckNodeReachable);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}

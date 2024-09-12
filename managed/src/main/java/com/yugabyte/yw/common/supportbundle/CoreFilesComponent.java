// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.List;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class CoreFilesComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  CoreFilesComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleUtil supportBundleUtil) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.supportBundleUtil = supportBundleUtil;
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    // pass
  }

  @Override
  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    String nodeHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    String coresDir = nodeHomeDir + "/cores/";
    bundlePath = Paths.get(bundlePath.toAbsolutePath().toString(), "cores");
    Files.createDirectories(bundlePath);

    // Get and filter the core files list based on the 2 params given in the request body.
    List<Pair<Long, String>> fileSizeNameList =
        nodeUniverseManager
            .getNodeFilePathsAndSize(node, universe, coresDir, startDate, endDate)
            .stream()
            .limit(supportBundleTaskParams.bundleData.maxNumRecentCores)
            .filter(p -> p.getFirst() <= supportBundleTaskParams.bundleData.maxCoreFileSize)
            .collect(Collectors.toList());

    // Filter the core files list based on the 2 params given in the request body.
    List<String> sourceNodeFiles =
        fileSizeNameList.stream().map(p -> p.getSecond()).collect(Collectors.toList());

    // Check if YBA node has enough space to store all cores files from the DB nodes.
    long YbaDiskSpaceFreeInBytes = Files.getFileStore(bundlePath).getUsableSpace();
    long coreFilesSize = fileSizeNameList.stream().mapToLong(Pair::getFirst).sum();

    // Throw error if YBA node doesn't have enough space to download the core files from this DB
    // node. Exception is caught in CreateSupportBundle.java to continue with rest of support bundle
    // execution.
    if (Long.compare(coreFilesSize, YbaDiskSpaceFreeInBytes) > 0) {
      String errMsg =
          String.format(
              "Cannot download core files due to insuffient space. Node: '%s', Core files size in"
                  + " bytes: '%d', YBA space free in bytes: '%d'.",
              node.nodeName, coreFilesSize, YbaDiskSpaceFreeInBytes);
      throw new RuntimeException(errMsg);
    }

    log.debug(
        "List of core files to get from the node '{}' after filtering: '{}'",
        node.nodeName,
        sourceNodeFiles);

    supportBundleUtil.downloadNodeLevelComponent(
        universeInfoHandler,
        customer,
        universe,
        bundlePath,
        node,
        coresDir,
        sourceNodeFiles,
        this.getClass().getSimpleName(),
        true /* skipUntar */);
  }
}

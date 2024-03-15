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
import java.nio.file.Path;
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
    String nodeHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    String coresDir = nodeHomeDir + "/cores/";
    List<Pair<Integer, String>> fileSizeNameList =
        nodeUniverseManager.getNodeFilePathsAndSize(node, universe, coresDir);

    // Filter the core files list based on the 2 params given in the request body.
    List<String> sourceNodeFiles =
        fileSizeNameList.stream()
            .limit(supportBundleTaskParams.bundleData.maxNumRecentCores)
            .filter(p -> p.getFirst() <= supportBundleTaskParams.bundleData.maxCoreFileSize)
            .map(p -> "cores/" + p.getSecond())
            .collect(Collectors.toList());

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
        nodeHomeDir,
        sourceNodeFiles,
        this.getClass().getSimpleName());
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
    this.downloadComponent(supportBundleTaskParams, customer, universe, bundlePath, node);
  }
}

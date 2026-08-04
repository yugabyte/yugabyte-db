// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
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

    Map<String, Long> fileNameSizeMap =
        getFilesListWithSizes(
            customer, supportBundleTaskParams.bundleData, universe, startDate, endDate, node);

    List<String> sourceNodeFiles = fileNameSizeMap.keySet().stream().collect(Collectors.toList());

    // Check if YBA node has enough space to store all cores files from the DB nodes.
    long YbaDiskSpaceFreeInBytes = Files.getFileStore(bundlePath).getUsableSpace();
    long coreFilesSize = fileNameSizeMap.values().stream().mapToLong(Long::longValue).sum();

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

  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {

    String nodeHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    String coresDir = nodeHomeDir + "/cores/";

    // Get and filter the core files list based on the 2 params given in the request body.
    Map<String, Long> fileNameSizeMap =
        nodeUniverseManager.getNodeFilePathsAndSizeWithinDates(
            node, universe, coresDir, startDate, endDate);
    fileNameSizeMap =
        filterFirstNEntries(
            fileNameSizeMap, bundleData.maxNumRecentCores, bundleData.maxCoreFileSize);
    return fileNameSizeMap;
  }

  /** Returns a map with the first numEntries which are <= given maxSize. */
  private Map<String, Long> filterFirstNEntries(
      Map<String, Long> map, int numEntries, long maxSize) {
    LinkedHashMap<String, Long> result = new LinkedHashMap<>();
    if (numEntries <= 0) {
      return result;
    }
    int count = 0;
    for (Map.Entry<String, Long> entry : map.entrySet()) {
      if (entry.getValue() <= maxSize) {
        result.put(entry.getKey(), entry.getValue());
        count++;
        if (count == numEntries) {
          break;
        }
      }
    }
    return result;
  }
}

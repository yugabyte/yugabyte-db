package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class ConsensusMetaComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  public static final List<String> sourceNodeFiles =
      Arrays.asList("master/consensus-meta", "tserver/consensus-meta");

  @Inject
  ConsensusMetaComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      Config config,
      SupportBundleUtil supportBundleUtil) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.config = config;
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
    // Downloads the /mnt/d0/master/consensus-meta and /mnt/d0/tserver/consensus-meta from each node
    // in the universe into the bundle path
    // Get source file path prefix
    String mountPath =
        supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
    String nodeHomeDir = mountPath + "/yb-data";
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

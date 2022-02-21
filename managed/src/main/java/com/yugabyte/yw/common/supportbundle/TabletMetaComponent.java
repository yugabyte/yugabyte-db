package com.yugabyte.yw.common.supportbundle;

import com.typesafe.config.Config;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.InstanceType;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.List;
import java.util.stream.Collectors;
import java.util.UUID;
import java.io.IOException;
import java.text.ParseException;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class TabletMetaComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  protected final Config config;

  @Inject
  TabletMetaComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      Config config) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.config = config;
  }

  @Override
  public void downloadComponent(Customer customer, Universe universe, Path bundlePath)
      throws IOException {
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    String destDir = bundlePath.toString() + "/" + "tablet_meta";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);

    // Downloads the /mnt/d0/master/tablet-meta and /mnt/d0/tserver/tablet-meta from each node in
    // the universe into the bundle path
    for (NodeDetails node : nodes) {
      // Get source file path prefix
      String mountPath =
          SupportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
      String nodeHomeDir = mountPath + "/yb-data";

      // Get target file path
      String nodeName = node.getNodeName();
      Path nodeTargetFile = Paths.get(destDir, nodeName + ".tar.gz");

      log.debug(
          "Gathering tablet meta for node: {}, source path: {}, target path: {}",
          nodeName,
          nodeHomeDir,
          nodeTargetFile.toString());

      Path targetFile =
          universeInfoHandler.downloadNodeFile(
              customer,
              universe,
              node,
              nodeHomeDir,
              "master/tablet-meta;tserver/tablet-meta",
              nodeTargetFile);
    }
  }

  @Override
  public void downloadComponentBetweenDates(
      Customer customer, Universe universe, Path bundlePath, Date startDate, Date endDate)
      throws IOException, ParseException {
    this.downloadComponent(customer, universe, bundlePath);
  }
}

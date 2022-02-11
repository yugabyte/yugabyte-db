package com.yugabyte.yw.common.supportbundle;

import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseManager;
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
class GFlagsComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  GFlagsComponent(
      UniverseInfoHandler universeInfoHandler, NodeUniverseManager nodeUniverseManager) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
  }

  @Override
  public void downloadComponent(Customer customer, Universe universe, Path bundlePath)
      throws IOException {
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    String destDir = bundlePath.toString() + "/" + "gflags";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);

    // Downloads the master/conf/server.conf and tserver/conf/server.conf from each node in the
    // universe into the bundle path
    for (NodeDetails node : nodes) {
      // Get source file path prefix
      String nodeHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);

      // Get target file path
      String nodeName = node.getNodeName();
      Path nodeTargetFile = Paths.get(destDir, nodeName + ".tar.gz");

      log.debug(
          "Gathering gflags conf for node: {}, source path: {}, target path: {}",
          nodeName,
          nodeHomeDir,
          nodeTargetFile.toString());

      Path targetFile =
          universeInfoHandler.downloadNodeFile(
              customer,
              universe,
              node,
              nodeHomeDir,
              "master/conf/server.conf;tserver/conf/server.conf",
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

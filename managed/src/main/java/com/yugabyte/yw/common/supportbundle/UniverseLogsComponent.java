package com.yugabyte.yw.common.supportbundle;

import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.List;
import java.util.stream.Collectors;
import java.io.IOException;
import java.text.ParseException;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class UniverseLogsComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;

  @Inject
  UniverseLogsComponent(UniverseInfoHandler universeInfoHandler) {
    this.universeInfoHandler = universeInfoHandler;
  }

  @Override
  public void downloadComponent(Customer customer, Universe universe, Path bundlePath)
      throws IOException {
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    String destDir = bundlePath.toString() + "/" + "universe_logs";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);

    // Downloads the master/logs and tserver/logs from each node in the universe into the bundle
    // path
    for (NodeDetails node : nodes) {
      String nodeName = node.getNodeName();
      Path nodeTargetFile = Paths.get(destDir, nodeName + ".tar.gz");
      log.debug("Creating node target file {}", nodeTargetFile.toString());
      Path targetFile =
          universeInfoHandler.downloadNodeLogs(customer, universe, node, nodeTargetFile);
    }
  }

  @Override
  public void downloadComponentBetweenDates(
      Customer customer, Universe universe, Path bundlePath, Date startDate, Date endDate)
      throws IOException, ParseException {
    // To fill
    this.downloadComponent(customer, universe, bundlePath);
  }
}

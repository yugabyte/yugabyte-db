package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstallThirdPartySoftwareK8s extends AbstractTaskBase {

  public enum SoftwareUpgradeType {
    XXHSUM
  }

  private final long UPLOAD_XXSUM_PACKAGE_TIMEOUT_SEC = 60;
  private final String PACKAGE_PERMISSIONS = "755";
  private NodeUniverseManager nodeUniverseManager;
  private Config appConfig;

  @Inject
  public InstallThirdPartySoftwareK8s(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      Config appConfig) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.appConfig = appConfig;
  }

  public static class Params extends AbstractTaskParams {
    public UUID universeUUID;
    public SoftwareUpgradeType softwareType;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    SoftwareUpgradeType softwareType = taskParams().softwareType;
    switch (softwareType) {
      case XXHSUM:
        installThirdPartyPackagesTaskK8s(universe);
        break;
    }
  }

  private Optional<String> getThirdpartyPackagePath() {
    String packagePath = appConfig.getString("yb.thirdparty.packagePath");
    if (packagePath != null && !packagePath.isEmpty()) {
      File thirdpartyPackagePath = new File(packagePath);
      if (thirdpartyPackagePath.exists() && thirdpartyPackagePath.isDirectory()) {
        return Optional.of(packagePath);
      }
    }
    return Optional.empty();
  }

  public void installThirdPartyPackagesTaskK8s(Universe universe) {
    Optional<String> packagePath = getThirdpartyPackagePath();
    if (!packagePath.isPresent()) {
      return;
    }
    String localPackagePath = packagePath.get();
    String xxhsumPackagePath = localPackagePath + "/xxhash.tar.gz";
    File f = new File(xxhsumPackagePath);
    if (!f.exists()) {
      log.warn(
          String.format("xxhsum binaries not present on YBA at %s, skipping", localPackagePath));
      return;
    }
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(false)
            .traceLogging(true)
            .timeoutSecs(UPLOAD_XXSUM_PACKAGE_TIMEOUT_SEC)
            .build();
    for (NodeDetails node : universe.getNodes()) {
      String xxhsumTargetPathParent = nodeUniverseManager.getYbTmpDir();
      String xxhsumTargetPath = xxhsumTargetPathParent + "/xxhash.tar.gz";
      nodeUniverseManager
          .uploadFileToNode(
              node, universe, xxhsumPackagePath, xxhsumTargetPath, PACKAGE_PERMISSIONS, context)
          .processErrors();

      String extractxxhSumBinaryCmd =
          String.format(
              "cd %s && tar -xvf xxhash.tar.gz && rm xxhash.tar.gz", xxhsumTargetPathParent);
      List<String> unTarxxhSumBinariesCmd = Arrays.asList("bash", "-c", extractxxhSumBinaryCmd);
      nodeUniverseManager
          .runCommand(node, universe, unTarxxhSumBinariesCmd, context)
          .processErrors();
    }
  }
}

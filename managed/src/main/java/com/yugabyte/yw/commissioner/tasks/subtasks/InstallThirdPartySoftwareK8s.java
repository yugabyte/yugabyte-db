package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstallThirdPartySoftwareK8s extends AbstractTaskBase {

  public enum SoftwareUpgradeType {
    XXHSUM,
    JWT_JWKS
  }

  private final long UPLOAD_PACKAGE_TIMEOUT_SEC = 60;
  private final String PACKAGE_PERMISSIONS = "755";
  private final String JWKS_FILE_PERMISSIONS = "644";
  private Config appConfig;

  @Inject
  public InstallThirdPartySoftwareK8s(BaseTaskDependencies baseTaskDependencies, Config appConfig) {
    super(baseTaskDependencies);
    this.appConfig = appConfig;
  }

  public static class Params extends AbstractTaskParams {
    public UUID universeUUID;
    public SoftwareUpgradeType softwareType;
    @Nullable public UniverseDefinitionTaskParams universeParams;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().universeUUID
        + ", type: "
        + taskParams().softwareType
        + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    // Set universe details clusters as task params clusters
    // Required for tasks like EditKubernetesUniverse where new AZs are added
    // and haven't been saved yet. Node details are saved.
    if (taskParams().universeParams != null) {
      universe.getUniverseDetails().clusters = taskParams().universeParams.clusters;
    }
    SoftwareUpgradeType softwareType = taskParams().softwareType;
    switch (softwareType) {
      case XXHSUM:
        installThirdPartyPackagesTaskK8s(universe);
        break;
      case JWT_JWKS:
        copyJWSKeys(universe);
        break;
    }
  }

  private void copyJWSKeys(Universe universe) {
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(false)
            .traceLogging(true)
            .timeoutSecs(UPLOAD_PACKAGE_TIMEOUT_SEC)
            .build();
    for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
      Map<String, String> tServerMap = new HashMap<>();
      try {
        tServerMap =
            cluster
                .userIntent
                .specificGFlags
                .getPerProcessFlags()
                .value
                .getOrDefault(ServerType.TSERVER, tServerMap);
      } catch (Exception e) {
        log.warn("gFlag not associated with universe, skipping. {}", e.getMessage());
        continue;
      }

      if (tServerMap.containsKey(GFlagsUtil.YSQL_HBA_CONF_CSV)) {
        String hbaConfValue = tServerMap.get(GFlagsUtil.YSQL_HBA_CONF_CSV);
        if (hbaConfValue.contains(GFlagsUtil.JWT_AUTH)) {
          Path tmpDirectoryPath =
              FileUtils.getOrCreateTmpDirectory(
                  confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath));
          Path localGflagFilePath = tmpDirectoryPath.resolve(cluster.uuid.toString());
          File[] jwksFiles = localGflagFilePath.toFile().listFiles();
          if (jwksFiles == null) {
            log.warn("No JWKS files generated at {}, skipping", localGflagFilePath);
            continue;
          }
          Provider provider = Util.getSingleProvider(cluster);
          String remoteGFlagPath = GFlagsUtil.getRemoteGFlagFilesPath(provider);
          // Only the tserver flags reference the JWKS files.
          List<NodeDetails> tserverNodes =
              universe.getUniverseDetails().getNodesInCluster(cluster.uuid).stream()
                  .filter(n -> n.isActive() && n.isTserver)
                  .collect(Collectors.toList());

          for (NodeDetails node : tserverNodes) {
            // Upload file by file: kubectl cp of a directory nests it inside the target when the
            // target already exists, and the target persists on the PV across tasks.
            for (File jwksFile : jwksFiles) {
              nodeUniverseManager.uploadFileToNode(
                  node,
                  universe,
                  jwksFile.getAbsolutePath(),
                  remoteGFlagPath + jwksFile.getName(),
                  JWKS_FILE_PERMISSIONS,
                  context);
            }
          }

          // Delete the local directory.
          FileUtils.deleteDirectory(localGflagFilePath.toFile());
        }
      }
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
            .timeoutSecs(UPLOAD_PACKAGE_TIMEOUT_SEC)
            .build();
    for (NodeDetails node : universe.getNodes()) {
      String xxhsumTargetPathParent = nodeUniverseManager.getYbTmpDir();
      String xxhsumTargetPath = xxhsumTargetPathParent + "/xxhash.tar.gz";
      nodeUniverseManager.uploadFileToNode(
          node, universe, xxhsumPackagePath, xxhsumTargetPath, PACKAGE_PERMISSIONS, context);

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

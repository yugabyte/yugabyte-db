// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.AbstractTaskParams;
import java.io.InputStream;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AddGFlagMetadata extends AbstractTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final ReleaseManager releaseManager;

  @Inject
  protected AddGFlagMetadata(
      BaseTaskDependencies baseTaskDependencies,
      GFlagsValidation gFlagsValidation,
      ReleaseManager releaseManager) {
    super(baseTaskDependencies);
    this.gFlagsValidation = gFlagsValidation;
    this.releaseManager = releaseManager;
  }

  public static class Params extends AbstractTaskParams {

    public String version;
    public ReleaseMetadata releaseMetadata;
    public List<String> requiredGFlagsFileList;
    public String releasesPath;
  }

  protected AddGFlagMetadata.Params taskParams() {
    return (AddGFlagMetadata.Params) taskParams;
  }

  public static void fetchGFlagFiles(
      ReleaseMetadata releaseMetadata,
      List<String> requiredGFlagsFileList,
      String version,
      String releasesPath,
      ReleaseManager releaseManager,
      GFlagsValidation gFlagsValidation) {
    try (InputStream tarGZIPInputStream = releaseManager.getTarGZipDBPackageInputStream(version)) {
      gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
          tarGZIPInputStream, version, requiredGFlagsFileList, releasesPath);
    } catch (Exception e) {
      log.error("Error in fetching GFlags metadata: {}", e.getMessage());
      throw new RuntimeException(e);
    }
  }

  @Override
  public void run() {
    try {
      fetchGFlagFiles(
          taskParams().releaseMetadata,
          taskParams().requiredGFlagsFileList,
          taskParams().version,
          taskParams().releasesPath,
          releaseManager,
          gFlagsValidation);
    } catch (RuntimeException e) {
      log.error("Task Errored out with: " + e);
      throw new RuntimeException(e);
    }
  }
}

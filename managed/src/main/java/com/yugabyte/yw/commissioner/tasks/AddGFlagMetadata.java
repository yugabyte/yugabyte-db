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
import org.apache.commons.lang3.StringUtils;

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

  @Override
  public void run() {
    try {
      ReleaseMetadata releaseMetadata = taskParams().releaseMetadata;
      List<String> requiredGFlagsFileList = taskParams().requiredGFlagsFileList;
      String version = taskParams().version;
      if (StringUtils.isEmpty(releaseMetadata.filePath)) {
        throw new RuntimeException(
            "Cannot add gFlags metadata for version: "
                + version
                + " as no DB package path was specified.");
      }
      try (InputStream tarGZIPInputStream =
          releaseManager.getTarGZipDBPackageInputStream(version, releaseMetadata)) {
        gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
            tarGZIPInputStream, version, requiredGFlagsFileList, taskParams().releasesPath);
      } catch (Exception e) {
        log.error(
            "Error in fetching GFlags metadata from the location: {}: {} ",
            releaseMetadata.filePath,
            e);
        throw new RuntimeException(e);
      }
    } catch (RuntimeException e) {
      log.error("Task Errored out with: " + e);
      throw new RuntimeException(e);
    }
  }
}

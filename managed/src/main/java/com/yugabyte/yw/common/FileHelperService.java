// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.FileUtils;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class FileHelperService {

  private RuntimeConfGetter confGetter;

  @Inject
  public FileHelperService(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public Path createTempFile(String fileName, String fileExtension) {
    Path tmpDirectoryPath =
        FileUtils.getOrCreateTmpDirectory(
            confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath));

    Path tmpFile = null;
    try {
      tmpFile = Files.createTempFile(tmpDirectoryPath, fileName, fileExtension);
    } catch (IOException e) {
      log.info("Error creating the tmp file", e.getMessage());
      // Let the caller handle the same.
      throw new RuntimeException("Could not create file", e);
    }

    return tmpFile;
  }
}

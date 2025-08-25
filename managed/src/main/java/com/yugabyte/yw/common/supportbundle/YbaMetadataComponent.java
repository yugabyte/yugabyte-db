// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;

@Slf4j
@Singleton
public class YbaMetadataComponent implements SupportBundleComponent {

  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  public final String YBA_METADATA_FOLDER = "metadata";
  @Inject RuntimeConfGetter confGetter;

  @Inject
  public YbaMetadataComponent(
      BaseTaskDependencies baseTaskDependencies, SupportBundleUtil supportBundleUtil) {
    this.config = baseTaskDependencies.getConfig();
    this.supportBundleUtil = supportBundleUtil;
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws IOException {
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
      throws IOException, ParseException {
    log.info("Gathering YBA Metadata json data for customer '{}'.", customer.getUuid());

    // Create YBA_METADATA_FOLDER folder inside the support bundle folder.
    String destDir = bundlePath.toString() + "/" + YBA_METADATA_FOLDER;
    Files.createDirectories(Paths.get(destDir));

    // Gather and save the YBA metadata.
    supportBundleUtil.gatherAndSaveAllMetadata(customer, universe, destDir, startDate, endDate);
  }

  // Collect the metadata in a temp dir and return the size of the directory.
  // Its only a matter of few db queries so shouldn't take too long.
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {

    // Create YBA_METADATA_FOLDER folder inside the support bundle folder.
    String destDir = getLocalTmpDir() + "/" + YBA_METADATA_FOLDER;
    File tmpMetadataDir = Files.createDirectories(Paths.get(destDir)).toFile();

    // Gather and save the YBA metadata.
    supportBundleUtil.gatherAndSaveAllMetadata(customer, universe, destDir, startDate, endDate);
    Map<String, Long> res = new HashMap<>();
    res.put(destDir, FileUtils.sizeOfDirectory(tmpMetadataDir));
    FileUtils.deleteDirectory(tmpMetadataDir);
    return res;
  }

  private String getLocalTmpDir() {
    String localTmpDir = confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath);
    if (localTmpDir == null || localTmpDir.isEmpty()) {
      localTmpDir = "/tmp";
    }
    return localTmpDir;
  }
}

// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.util.Date;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class YbaMetadataComponent implements SupportBundleComponent {

  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  public final String YBA_METADATA_FOLDER = "metadata";

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
    log.info("Gathering call home json data for customer '{}'.", customer.getUuid());

    // Create YBA_METADATA_FOLDER folder inside the support bundle folder.
    String destDir = bundlePath.toString() + "/" + YBA_METADATA_FOLDER;
    Files.createDirectories(Paths.get(destDir));

    // Gather and save the YBA call home data.
    supportBundleUtil.gatherAndSaveAllMetadata(customer, destDir);
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
    this.downloadComponent(supportBundleTaskParams, customer, universe, bundlePath, node);
  }
}

package com.yugabyte.yw.common.diagnostics;

import com.typesafe.config.ConfigException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

/**
 * Publisher for support bundle files to various destinations. Extends BasePublisher to eliminate
 * code duplication.
 */
@Slf4j
@Singleton
public class SupportBundlePublisher extends BasePublisher<File> {

  @Inject
  public SupportBundlePublisher(RuntimeConfigFactory runtimeConfigFactory) {
    super(runtimeConfigFactory, "yba_support_bundle");
    initializeDestinations();
  }

  @Override
  protected void createDestinations() {
    try {
      destinations.put("gcs", new SupportBundleGCSDestination(runtimeConfigFactory));
      publishFailureCounter.withTag(DESTINATION_TAG, "gcs").increment(0);
    } catch (ConfigException e) {
      log.error("Missing or invalid GCS config for support bundles", e);
      publishFailureCounter.withTag(DESTINATION_TAG, "gcs").increment(1);
    }
  }

  /**
   * Publishes a support bundle file to all enabled destinations with universe context. This
   * generates universe-specific paths for better organization.
   *
   * @param supportBundleFile The support bundle file to upload
   * @param universe The universe context for path generation
   * @return true if at least one destination succeeded, false otherwise
   */
  public boolean publish(File supportBundleFile, Universe universe) {
    if (supportBundleFile == null || !supportBundleFile.exists()) {
      log.error("Support bundle file is null or does not exist");
      return false;
    }

    String blobPath =
        String.join("/", universe.getUniverseUUID().toString(), supportBundleFile.getName());
    return publishToEnabledDestinations(supportBundleFile, blobPath);
  }
}

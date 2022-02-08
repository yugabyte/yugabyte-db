package com.yugabyte.yw.common.supportbundle;

import com.typesafe.config.Config;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.io.File;
import java.io.IOException;
import java.util.Date;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;

@Slf4j
@Singleton
public class ApplicationLogsComponent implements SupportBundleComponent {

  protected final Config config;

  @Inject
  public ApplicationLogsComponent(BaseTaskDependencies baseTaskDependencies) {
    this.config = baseTaskDependencies.getConfig();
  }

  @Override
  public void downloadComponent(Customer customer, Universe universe, Path bundlePath)
      throws IOException {
    String appHomeDir =
        config.hasPath("application.home") ? config.getString("application.home") : ".";
    String logDir =
        config.hasPath("log.override.path")
            ? config.getString("log.override.path")
            : String.format("%s/logs", appHomeDir);
    String destDir = bundlePath.toString() + "/" + "application_logs";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);
    File source = new File(logDir);
    File dest = new File(destDir);
    FileUtils.copyDirectory(source, dest);
    log.debug("Downloaded application logs to {}", destDir);
  }

  @Override
  public void downloadComponentBetweenDates(
      Customer customer, Universe universe, Path bundlePath, Date startDate, Date endDate) {
    // To fill
  };
}

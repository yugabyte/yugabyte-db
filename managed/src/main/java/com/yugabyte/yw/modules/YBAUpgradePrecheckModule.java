// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.modules;

import com.google.inject.AbstractModule;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.YBAUpgradePrecheck;
import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.Environment;

@Slf4j
@Singleton
public class YBAUpgradePrecheckModule extends AbstractModule {
  private static final String UPGRADE_PRECHECK_PROP = "yba_upgrade_precheck";
  private static final String UPGRADE_PRECHECK_OUTPUT_DIR_PROP = "yba_upgrade_precheck_output_dir";
  private static final String DEFAULT_PRECHECK_ONLY_OUTPUT_DIR = "/tmp/yba_upgrade_precheck";

  @Inject
  public YBAUpgradePrecheckModule(Environment environment, Config config) {
    String upgradePrecheck = getPropertyValue(UPGRADE_PRECHECK_PROP);
    if (!"true".equalsIgnoreCase(StringUtils.trim(upgradePrecheck))) {
      log.info("Skipping YBA upgrade precheck as it is disabled");
      return;
    }
    String upgradePrecheckOutputDir = getPropertyValue(UPGRADE_PRECHECK_OUTPUT_DIR_PROP);
    // Set output dir only if it's precheck and is not set to prevent normal startup failure.
    Path precheckOutputDirPath =
        Paths.get(
            StringUtils.isBlank(upgradePrecheckOutputDir)
                ? DEFAULT_PRECHECK_ONLY_OUTPUT_DIR
                : upgradePrecheckOutputDir);
    File file = precheckOutputDirPath.toFile();
    if (!file.exists()) {
      file.mkdirs();
    }
    try {
      log.info("Running upgrade precheck");
      if (!new YBAUpgradePrecheck(config).run(precheckOutputDirPath)) {
        log.warn("YBA upgrade precheck failed");
      }
    } finally {
      log.info("Finished upgrade precheck");
    }
    System.exit(0);
  }

  private String getPropertyValue(String name) {
    String value = System.getProperty(name);
    if (StringUtils.isBlank(value)) {
      value = System.getenv(name.toUpperCase());
    }
    return value;
  }
}

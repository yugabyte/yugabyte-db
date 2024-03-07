/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.controllers.handlers.EnvProxySelector;
import io.ebean.DB;
import java.net.ProxySelector;
import lombok.extern.slf4j.Slf4j;
import play.Environment;
import play.db.ebean.EbeanDynamicEvolutions;
import play.mvc.Http.Status;

/** Play lifecycle does not give onStartup event */
@Slf4j
public class YBALifeCycle {

  private static final String ENV_PROXY_SELECTOR_PARAM = "yb.env_proxy_selector.enabled";

  private final Config config;
  private final ConfigHelper configHelper;
  private final Environment environment;

  @Inject
  public YBALifeCycle(
      Config config,
      ConfigHelper configHelper,
      Environment environment,
      StaticInjectorHolder staticInjectorHolder,
      EbeanDynamicEvolutions ebeanDynamicEvolutions) {
    this.config = config;
    this.configHelper = configHelper;
    this.environment = environment;
    onStart();
  }

  /** This is invoked before any migrations start and first thing after YBA module is loaded. */
  void onStart() {
    if (config.getBoolean(ENV_PROXY_SELECTOR_PARAM)) {
      log.info("Env proxy selector enabled");
      ProxySelector.setDefault(new EnvProxySelector());
    }
    checkIfDowngrade();
  }

  /**
   * Check if this is a downgrade and fail if downgrades are not allowed per configuration setting
   * `yb.is_platform_downgrade_allowed`
   */
  private void checkIfDowngrade() {
    boolean isFreshInstall =
        !DB.getDefault()
            .sqlQuery("SELECT * FROM information_schema.tables WHERE table_name = 'schema_version'")
            .findOneOrEmpty()
            .isPresent();
    if (isFreshInstall) {
      return;
    }
    String version = ConfigHelper.getCurrentVersion(environment);

    String previousSoftwareVersion =
        configHelper
            .getConfig(ConfigHelper.ConfigType.YugawareMetadata)
            .getOrDefault("version", "")
            .toString();

    boolean isPreviousVersionStable = Util.isStableVersion(previousSoftwareVersion, false);
    boolean isCurrentVersionStable = Util.isStableVersion(version, false);
    // Skip version checks if runtime flag enabled. User must take care of downgrades
    if (!config.getBoolean(GlobalConfKeys.skipVersionChecks.getKey())) {
      if (isPreviousVersionStable ^ isCurrentVersionStable) {
        String msg =
            String.format(
                "Cannot upgrade from preview to stable version or stable to preview. If required,"
                    + " set runtime flag 'yb.skip_version_checks' to true. Tried to upgrade from"
                    + " '%s' to '%s'.",
                previousSoftwareVersion, version);
        throw new PlatformServiceException(Status.BAD_REQUEST, msg);
      }
    }

    boolean isPlatformDowngradeAllowed = config.getBoolean("yb.is_platform_downgrade_allowed");

    if (Util.compareYbVersions(previousSoftwareVersion, version, true) > 0
        && !isPlatformDowngradeAllowed) {

      String msg =
          String.format(
              "Platform does not support version downgrades, %s"
                  + " has downgraded to %s. Shutting down. To override this check"
                  + " (not recommended) and continue startup,"
                  + " set the application config setting yb.is_platform_downgrade_allowed"
                  + "or the environment variable"
                  + " YB_IS_PLATFORM_DOWNGRADE_ALLOWED to true."
                  + " Otherwise, upgrade your YBA version back to or above %s to proceed.",
              previousSoftwareVersion, version, previousSoftwareVersion);

      throw new RuntimeException(msg);
    }
  }
}

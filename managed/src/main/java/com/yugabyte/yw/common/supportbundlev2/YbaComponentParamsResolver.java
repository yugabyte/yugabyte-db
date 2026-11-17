// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.ensureFlagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.formatDay;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.formatUniverseLogsInstant;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.replaceFlagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.truncateToDay;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import java.nio.file.Paths;
import java.util.Date;
import java.util.List;

/** Resolves the params of YBAComponent members, which run on the YBA host. */
final class YbaComponentParamsResolver {

  private static final String APPLICATION_LOGS_COMPONENT = ComponentType.ApplicationLogs.name();

  private YbaComponentParamsResolver() {}

  static List<String> resolve(
      List<String> params,
      String componentName,
      Date startDate,
      Date endDate,
      Config config,
      RuntimeConfGetter confGetter) {
    if (APPLICATION_LOGS_COMPONENT.equals(componentName)) {
      return resolveApplicationLogsParams(params, startDate, endDate, config, confGetter);
    }
    return resolveDateRangeParams(params, startDate, endDate);
  }

  private static List<String> resolveApplicationLogsParams(
      List<String> params,
      Date startDate,
      Date endDate,
      Config config,
      RuntimeConfGetter confGetter) {
    List<String> resolved = params;
    if (startDate != null) {
      resolved =
          replaceFlagValue(
              resolved, "--start_date", formatUniverseLogsInstant(truncateToDay(startDate)));
    }
    if (endDate != null) {
      resolved = replaceFlagValue(resolved, "--end_date", formatUniverseLogsInstant(endDate));
    }
    if (config != null) {
      String logDir =
          Paths.get(config.getString("log.override.path")).toAbsolutePath().normalize().toString();
      resolved = replaceFlagValue(resolved, "--log_dir", logDir);
    }
    if (confGetter != null) {
      resolved =
          ensureFlagValue(
              resolved,
              "--application_logs_regex",
              confGetter.getGlobalConf(GlobalConfKeys.supportBundleApplicationLogsRegexPattern));
      resolved =
          ensureFlagValue(
              resolved,
              "--application_logs_sdf",
              confGetter.getGlobalConf(GlobalConfKeys.supportBundleApplicationLogsSdfPattern));
    }
    return resolved;
  }

  private static List<String> resolveDateRangeParams(
      List<String> params, Date startDate, Date endDate) {
    List<String> resolved = params;
    if (startDate != null) {
      resolved = replaceFlagValue(resolved, "--start_date", formatDay(truncateToDay(startDate)));
    }
    if (endDate != null) {
      resolved = replaceFlagValue(resolved, "--end_date", formatDay(endDate));
    }
    return resolved;
  }
}

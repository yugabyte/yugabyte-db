// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import org.apache.commons.lang3.time.DateUtils;

/**
 * Shared by the component param resolvers. Params are a flat {@code --flag value} list, rewritten
 * by flag name rather than by index so a manifest may order or omit flags freely; every method
 * returns a new list.
 */
final class ScriptParamsFlags {

  // Immutable, so estimates can format from several component threads at once; the shared
  // SimpleDateFormats these replaced could not. Zones are unchanged: the day formatter follows the
  // JVM default, matching truncateToDay and V1's per-call formatter in SupportBundleUtil, while the
  // instant formatter stays UTC. Locale.ROOT pins ASCII digits and the ISO calendar, which a
  // locale-defaulted SimpleDateFormat did not guarantee.
  private static final DateTimeFormatter DATE_FORMAT =
      DateTimeFormatter.ofPattern("yyyy-MM-dd", Locale.ROOT).withZone(ZoneId.systemDefault());
  private static final DateTimeFormatter UNIVERSE_LOGS_INSTANT_FORMAT =
      DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.ROOT).withZone(ZoneOffset.UTC);

  private ScriptParamsFlags() {}

  /** Replaces the value of a flag already present; a no-op when the flag is absent. */
  static List<String> replaceFlagValue(List<String> params, String flag, String value) {
    List<String> result = new ArrayList<>(params.size());
    for (int i = 0; i < params.size(); i++) {
      if (flag.equals(params.get(i)) && i + 1 < params.size()) {
        result.add(flag);
        result.add(value);
        i++;
      } else {
        result.add(params.get(i));
      }
    }
    return result;
  }

  /** As {@link #replaceFlagValue}, but appends the flag when the manifest did not include it. */
  static List<String> ensureFlagValue(List<String> params, String flag, String value) {
    for (int i = 0; i < params.size() - 1; i++) {
      if (flag.equals(params.get(i))) {
        return replaceFlagValue(params, flag, value);
      }
    }
    List<String> result = new ArrayList<>(params.size() + 2);
    result.addAll(params);
    result.add(flag);
    result.add(value);
    return result;
  }

  static int indexOfFlag(List<String> params, String flag) {
    for (int i = 0; i < params.size(); i++) {
      if (flag.equals(params.get(i))) {
        return i;
      }
    }
    return -1;
  }

  /**
   * Log file names carry a date, not a time, so the window has to be compared at day granularity or
   * a file whose name falls on the boundary day is dropped.
   */
  static Date truncateToDay(Date date) {
    return DateUtils.truncate(date, Calendar.DAY_OF_MONTH);
  }

  static String formatDay(Date date) {
    return DATE_FORMAT.format(date.toInstant());
  }

  static String formatUniverseLogsInstant(Date date) {
    return UNIVERSE_LOGS_INSTANT_FORMAT.format(date.toInstant());
  }

  static String getOverriddenGflagValue(
      Universe universe, UniverseTaskBase.ServerType serverType, String gflag) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.specificGFlags == null
        || userIntent.specificGFlags.getPerProcessFlags() == null) {
      return null;
    }
    Map<UniverseTaskBase.ServerType, Map<String, String>> specificGflags =
        userIntent.specificGFlags.getPerProcessFlags().value;
    if (specificGflags != null && specificGflags.containsKey(serverType)) {
      Map<String, String> gflags = specificGflags.get(serverType);
      if (gflags.containsKey(gflag)) {
        return gflags.get(gflag);
      }
    }
    return null;
  }
}

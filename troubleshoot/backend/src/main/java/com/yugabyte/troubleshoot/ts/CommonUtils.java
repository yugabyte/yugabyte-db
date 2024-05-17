package com.yugabyte.troubleshoot.ts;

import java.io.IOException;
import java.nio.charset.Charset;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeFormatterBuilder;
import java.time.temporal.ChronoField;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.springframework.core.io.ClassPathResource;
import org.springframework.util.StreamUtils;

@Slf4j
public class CommonUtils {

  public static final DateTimeFormatter PG_TIMESTAMP_FORMAT =
      new DateTimeFormatterBuilder()
          .append(DateTimeFormatter.ISO_LOCAL_DATE)
          .appendLiteral('T')
          .appendPattern("HH:mm:ss")
          .appendFraction(ChronoField.NANO_OF_SECOND, 1, 9, true)
          .appendPattern("xxx")
          .toFormatter();

  public static final String SYSTEM_PLATFORM = "system_platform";

  private static final Pattern RELEASE_REGEX =
      Pattern.compile("^(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)(.+)?$");

  public static boolean isReleaseEqualOrAfter(String thresholdRelease, String actualRelease) {
    return compareReleases(thresholdRelease, actualRelease, false, true, true);
  }

  public static boolean isReleaseBefore(String thresholdRelease, String actualRelease) {
    return compareReleases(thresholdRelease, actualRelease, true, false, false);
  }

  public static boolean isReleaseBetween(
      String minRelease, String maxRelease, String actualRelease) {
    return isReleaseEqualOrAfter(minRelease, actualRelease)
        && isReleaseBefore(maxRelease, actualRelease);
  }

  public static boolean isReleaseEqual(String thresholdRelease, String actualRelease) {
    return compareReleases(thresholdRelease, actualRelease, false, false, true);
  }

  private static boolean compareReleases(
      String thresholdRelease,
      String actualRelease,
      boolean beforeMatches,
      boolean afterMatches,
      boolean equalMatches) {
    Matcher thresholdMatcher = RELEASE_REGEX.matcher(thresholdRelease);
    Matcher actualMatcher = RELEASE_REGEX.matcher(actualRelease);
    if (!thresholdMatcher.matches()) {
      throw new IllegalArgumentException(
          "Threshold release " + thresholdRelease + " does not match release pattern");
    }
    if (!actualMatcher.matches()) {
      log.warn(
          "Actual release {} does not match release pattern - handle as latest release",
          actualRelease);
      return afterMatches;
    }
    for (int i = 1; i < 6; i++) {
      String thresholdPartStr = thresholdMatcher.group(i);
      String actualPartStr = actualMatcher.group(i);
      if (i == 5) {
        // Build number.
        thresholdPartStr = String.valueOf(convertBuildNumberForComparison(thresholdPartStr, true));
        actualPartStr = String.valueOf(convertBuildNumberForComparison(actualPartStr, false));
      }
      int thresholdPart = Integer.parseInt(thresholdPartStr);
      int actualPart = Integer.parseInt(actualPartStr);
      if (actualPart > thresholdPart) {
        return afterMatches;
      }
      if (actualPart < thresholdPart) {
        return beforeMatches;
      }
    }
    // Equal releases.
    return equalMatches;
  }

  private static int convertBuildNumberForComparison(String buildNumberStr, boolean threshold) {
    if (StringUtils.isEmpty(buildNumberStr) || !buildNumberStr.startsWith("-b")) {
      // Threshold without a build or with invalid build is treated as -b0,
      // while actual build is custom build and is always treated as later build.
      return threshold ? 0 : Integer.MAX_VALUE;
    }
    try {
      return Integer.parseInt(buildNumberStr.substring(2));
    } catch (Exception e) {
      // Same logic as above.
      return threshold ? 0 : Integer.MAX_VALUE;
    }
  }

  public static String readResource(String path) {
    try {
      return StreamUtils.copyToString(
          new ClassPathResource(path).getInputStream(), Charset.defaultCharset());
    } catch (IOException e) {
      throw new RuntimeException("Failed to read resource: " + path, e);
    }
  }
}

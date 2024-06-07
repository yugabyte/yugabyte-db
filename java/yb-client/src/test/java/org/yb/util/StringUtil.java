// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.util;

import java.math.RoundingMode;
import java.text.DecimalFormat;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

public final class StringUtil {

  private final static Pattern RTRIM_RE = Pattern.compile("\\s+$");

  private StringUtil() {
  }

  public static String joinLinesForLogging(List<String> lines) {
    return joinLinesForLoggingHelper(lines, "    ");
  }

  public static String joinLinesForLoggingNoPrefix(List<String> lines) {
    return joinLinesForLoggingHelper(lines, "");
  }

  private static String joinLinesForLoggingHelper(List<String> lines, String prefix) {
    return lines.stream().map(s -> prefix + s).collect(Collectors.joining("\n"));
  }

  public static boolean isStringTrue(String value, boolean defaultValue) {
    if (value == null)
      return defaultValue;
    value = value.trim().toLowerCase();
    if (value.isEmpty() || value.equals("auto") || value.equals("default"))
      return defaultValue;
    return Arrays.asList("1", "yes", "y", "on", "enabled", "true", "t").contains(value);
  }

  public static String expandTabs(String line) {
    StringBuilder sb = new StringBuilder();
    final int tabSize = 8;
    for (int lineIndex = 0; lineIndex < line.length(); ++lineIndex) {
      char c = line.charAt(lineIndex);
      if (c == '\t') {
        int nSpaces = tabSize - sb.length() % tabSize;
        for (int spaceIndex = 0; spaceIndex < nSpaces; ++spaceIndex) {
          sb.append(' ');
        }
      } else {
        sb.append(c);
      }
    }
    return sb.toString();
  }

  public static String expandTabsAndConcatenate(List<String> lines) {
    return lines.stream().map(StringUtil::expandTabs).collect(Collectors.joining("\n"));
  }

  public static String rtrim(String s) {
    return RTRIM_RE.matcher(s).replaceAll("");
  }

  public static List<String> expandTabsAndRemoveTrailingSpaces(List<String> lines) {
    return lines.stream()
        .map(s -> expandTabsAndRemoveTrailingSpaces(s))
        .collect(Collectors.toList());
  }

  public static String expandTabsAndRemoveTrailingSpaces(String s) {
    return rtrim(expandTabs(s));
  }

  public static int getMaxLineLength(List<String> lines) {
    return lines.stream().mapToInt(s -> s.length()).max().orElse(0);
  }

  /**
   * Returns an environment variable dump of the form
   * <code>
   * ENV_VAR_NAME_1: ENV_VAR_VALUE1
   * ENV_VAR_NAME_2: ENV_VAR_VALUE2
   * </code>
   * @param envVars the input map of environment variables
   * @param separator the separator, e.g. {@code ": "} for the example above.
   * @param indentation the number of spaces to indent each line with
   * @return the formatted environment variable dump.
   */
  public static String getEnvVarMapDumpStr(
      Map<String, String> envVars, String separator, int indentation) {
    List<String> envVarDump = new ArrayList<>();
    for (Map.Entry<String, String> entry : envVars.entrySet()) {
      envVarDump.add(entry.getKey() + separator + entry.getValue());
    }
    Collections.sort(envVarDump);
    String indentStr = String.join("", Collections.nCopies(indentation, " "));
    return indentStr + String.join("\n" + indentStr, envVarDump);
  }

  /**
   * Formats double as a decimal string, leaving up to {@code maxFractionDigits} digits after
   * decimal separator and truncating the rest.
   */
  public static String toDecimalString(double dbl, int maxFractionDigits) {
    NumberFormat df = DecimalFormat.getInstance();
    df.setMaximumFractionDigits(maxFractionDigits);
    df.setRoundingMode(RoundingMode.DOWN);
    return df.format(dbl);
  }
}

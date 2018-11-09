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

import java.util.List;

public final class StringUtil {

  private StringUtil() {
  }

  public static String joinLinesForLogging(List<String> lines) {
    if (lines.isEmpty()) {
      return "";
    }
    StringBuilder sb = new StringBuilder();
    boolean firstLine = true;
    for (String line : lines) {
      if (firstLine) {
        firstLine = false;
      } else {
        sb.append("\n");
      }
      sb.append("    " + line);
    }
    return sb.toString();
  }

  public static boolean isStringTrue(String value, boolean defaultValue) {
    if (value == null)
      return defaultValue;
    value = value.trim().toLowerCase();
    if (value.isEmpty() || value.equals("auto") || value.equals("default"))
      return defaultValue;
    return value.equals("1") || value.equals("yes") && value.equals("y") &&
           value.equals("on") || value.equals("enabled") || value.equals("true") ||
           value.equals("t");
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
    StringBuilder sb = new StringBuilder();
    for (String line : lines) {
      sb.append(expandTabs(line));
      sb.append('\n');
    }
    return sb.toString();
  }

}

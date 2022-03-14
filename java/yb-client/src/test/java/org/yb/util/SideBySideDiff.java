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

import org.apache.commons.io.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;

import java.io.File;
import java.io.IOException;
import java.util.*;

/**
 * Produces a human-readable side-by-side diff between two files.
 */
public class SideBySideDiff {

  private static final Logger LOG = LoggerFactory.getLogger(SideBySideDiff.class);

  private File f1, f2;

  private List<String> sxsDiffLines;

  /**
   * Zero-based column of the middle of the side-by-side diff output as produced by the diff
   * command.
   */
  private int midColumn;

  private static Set<Character> VALID_MID_CHAR_SUPERSET = new TreeSet<>(
      Arrays.asList(new Character[] {' ', '<', '>', '|'})
  );

  private static Set<Character> VALID_LEFT_RIGHT_CHAR_SUPERSET = new TreeSet<>(
      Arrays.asList(new Character[] {' '})
  );

  /**
   * charSetAtColumn[i] is the the set of characters in the diff at the 0-based column i.
   * This is used to find the divider that is not always exactly in the middle of the width
   * passed to the diff command.
   */
  private List<Set<Character>> charSetAtColumn = new ArrayList<>();

  /**
   * This width is given to the side-by-side diff call. Not necessarily the final displayed width.
   */

  public SideBySideDiff(File f1, File f2) {
    this.f1 = f1;
    this.f2 = f2;
  }

  private Set<Character> getCharSetAtColumn(int i) {
    while (i >= charSetAtColumn.size()) {
      charSetAtColumn.add(null);
    }
    Set<Character> set = charSetAtColumn.get(i);
    if (set != null) {
      return set;
    }

    set = new TreeSet<>();
    charSetAtColumn.set(i, set);

    for (String line : sxsDiffLines) {
      if (i < line.length()) {
        set.add(line.charAt(i));
      }
    }
    return set;
  }

  private static List<String> readLinesAndNormalize(File f) throws IOException {
    return StringUtil.expandTabsAndRemoveTrailingSpaces(FileUtil.readLinesFrom(f));
  }

  private static void sanityCheckLinesMatch(
      String fileDescription, List<String> lines, int i, String lineFromDiff) {
    if (i < 0 || i >= lines.size()) {
      LOG.error("SideBySideDiff sanity check failed: trying to get line " + (i + 1) + " in the " +
          fileDescription + " file (it has " + lines.size() + " lines)");
      return;
    }
    String lineFromFile = lines.get(i);
    if (!lineFromFile.equals(lineFromDiff)) {
      LOG.error("SideBySideDiff sanity check failed: line " + (i + 1) + " from the " +
          fileDescription + " file is\n" + lineFromFile + "<EOL>" +
          "\nbut the diff tool implies it should be\n" + lineFromDiff + "<EOL>");
    }
  }

  private static class SideBySideDiffLine {
    int i1, i2;
    String s1, s2;
    char diffChar;

    public SideBySideDiffLine(int i1, String s1, int i2, String s2, char diffChar) {
      this.i1 = i1;
      this.s1 = s1;
      this.i2 = i2;
      this.s2 = s2;
      this.diffChar = diffChar;
    }

    String getLeftLineNumStr() {
      if (diffChar != '>') {
        return String.valueOf(i1 + 1);
      }
      return "";
    }

    String getRightLineNumStr() {
      if (diffChar != '<') {
        return String.valueOf(i2 + 1);
      }
      return "";
    }
  }

  public String getSideBySideDiff() throws IOException {
    StringBuilder result = new StringBuilder();
    List<String> lines1 = readLinesAndNormalize(f1);
    List<String> lines2 = readLinesAndNormalize(f2);

    final int diffWidth = Math.max(
        StringUtil.getMaxLineLength(lines1),
        StringUtil.getMaxLineLength(lines2)) * 2 + 4;

    // Diff has no way to strip trailing spaces, so we do preprocessing for it.
    File f1copy = new File(TestUtils.getBaseTmpDir(), "f1_" + f1.getName());
    File f2copy = new File(TestUtils.getBaseTmpDir(), "f2_" + f2.getName());
    FileUtils.writeLines(f1copy, lines1);
    FileUtils.writeLines(f2copy, lines2);

    String diffCmd = String.format("diff --width=%d --side-by-side '%s' '%s'",
        diffWidth, f1copy, f2copy);
    CommandResult commandResult = CommandUtil.runShellCommand(diffCmd);
    List<String> stdoutLines = commandResult.getStdoutLines();

    sxsDiffLines = new ArrayList<>();

    int maxSxsDiffLineLength = 0;
    for (String line : stdoutLines) {
      String expandedLine = StringUtil.expandTabs(line);
      sxsDiffLines.add(expandedLine);
      maxSxsDiffLineLength = Math.max(maxSxsDiffLineLength, expandedLine.length());
    }

    // This is not necessarily exactly where the mid-column is, but pretty close to it in practice.
    final int midColumnGuess = diffWidth / 2 - 1;

    midColumn = -1;

    outerLoop:
    for (int offsetFromMiddle = 0; offsetFromMiddle <= maxSxsDiffLineLength / 2;
         ++offsetFromMiddle) {
      for (int offsetDirection = -1; offsetDirection <= 1; offsetDirection += 2) {
        int offset = midColumnGuess + offsetDirection * offsetFromMiddle;
        if (offset >= 0 && offset < maxSxsDiffLineLength) {
          Set<Character> leftSet = getCharSetAtColumn(offset - 1);
          Set<Character> midSet = getCharSetAtColumn(offset);
          Set<Character> rightSet = getCharSetAtColumn(offset + 1);
          if (VALID_LEFT_RIGHT_CHAR_SUPERSET.containsAll(leftSet) &&
              VALID_LEFT_RIGHT_CHAR_SUPERSET.containsAll(rightSet) &&
              VALID_MID_CHAR_SUPERSET.containsAll(midSet)) {
            midColumn = offset;
            break outerLoop;
          }
        }
      }
    }

    if (midColumn == -1) {
      LOG.error("Side-by-side diff raw output with tabs expanded:\n" +
          StringUtil.expandTabsAndConcatenate(sxsDiffLines));
      throw new IOException(
          "Was not able to find the mid-column of the side-by-side diff. " +
              "See the raw output with tabs expanded in the log.");
    }

    int i1 = 0;
    int i2 = 0;
    List<SideBySideDiffLine> outLines = new ArrayList<>();
    for (String diffLine : sxsDiffLines) {
      String s1 = diffLine.substring(0, Math.min(midColumn - 1, diffLine.length()));
      s1 = StringUtil.rtrim(s1);

      int offset2 = midColumn + 3;
      String s2 = diffLine.length() > offset2 ? diffLine.substring(offset2) : "";
      s2 = StringUtil.rtrim(s2);

      char diffChar = midColumn < diffLine.length() ? diffLine.charAt(midColumn) : ' ';

      outLines.add(new SideBySideDiffLine(i1, s1, i2, s2, diffChar));
      if (diffChar != '<') {
        // "<" would mean this line is unique to the left-hand-side file.
        sanityCheckLinesMatch("rhs", lines2, i2, s2);
        i2++;
      }
      if (diffChar != '>') {
        // ">" would mean this line is unique to the right-hand-side file.
        sanityCheckLinesMatch("lhs", lines1, i1, s1);
        i1++;
      }
    }

    int valueWidth1 = 0;
    for (SideBySideDiffLine sxsLine : outLines) {
      valueWidth1 = Math.max(valueWidth1, sxsLine.s1.length());
    }
    int lineNumWidth1 = String.valueOf(lines1.size()).length();
    int lineNumWidth2 = String.valueOf(lines2.size()).length();
    String fmt = "%" + lineNumWidth1 + "s  " +
        "%-" + valueWidth1 + "s  %c  %" + lineNumWidth2 + "s  %s";
    for (SideBySideDiffLine sxsLine: outLines) {
      result.append(StringUtil.rtrim(
          String.format(fmt, sxsLine.getLeftLineNumStr(), sxsLine.s1, sxsLine.diffChar,
              sxsLine.getRightLineNumStr(), sxsLine.s2)));
      result.append('\n');
    }

    return result.toString();
  }

}

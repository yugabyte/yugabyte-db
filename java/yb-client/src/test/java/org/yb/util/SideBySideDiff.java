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

import static org.yb.AssertionWrappers.*;

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

  private static Set<Character> VALID_MID_CHAR_SUPERSET = new TreeSet<>(
      Arrays.asList(' ', '<', '>', '|')
  );

  private static Set<Character> VALID_LEFT_RIGHT_CHAR_SUPERSET = new TreeSet<>(
      Arrays.asList(' ')
  );

  /**
   * This width is given to the side-by-side diff call. Not necessarily the final displayed width.
   */
  public SideBySideDiff(File f1, File f2) {
    this.f1 = f1;
    this.f2 = f2;
  }

  private List<Set<Character>> getColumnCharacters(List<String> diffLines, int maxDiffLineLength) {
    List<Set<Character>> result = new ArrayList<>();
    for (int i = 0; i < maxDiffLineLength; ++i) {
      result.add(new TreeSet<>());
    }
    for (String line : diffLines) {
      for (int i = 0; i < line.length(); ++i) {
        result.get(i).add(line.charAt(i));
      }
    }
    return result;
  }

  private static List<String> readLinesAndNormalize(File f) throws IOException {
    return StringUtil.expandTabsAndRemoveTrailingSpaces(FileUtil.readLinesFrom(f));
  }

  private static void sanityCheckLinesMatch(
      String fileDescription, List<String> lines, int i, String lineFromDiff) {
    if (i < 0 || i >= lines.size()) {
      throw new RuntimeException(
          "SideBySideDiff sanity check failed: trying to get line " + (i + 1) + " in the " +
              fileDescription + " file (it has " + lines.size() + " lines)");
    }
    String lineFromFile = lines.get(i);
    if (!lineFromFile.equals(lineFromDiff)) {
      throw new RuntimeException(
          "SideBySideDiff sanity check failed: line " + (i + 1) + " from the " +
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

    // Diff lines look like:
    // LHS | RHS
    //
    // The mid-char | can instead be space (for no change), <, or >.
    //
    // From experimenting, sometimes sdiff on Linux uses extra space around the mid-char.
    // So we give 9 spaces in addition to the worst case length (where both sides are of
    // equal length).
    final int diffWidth = Math.max(
        StringUtil.getMaxLineLength(lines1),
        StringUtil.getMaxLineLength(lines2)) * 2 + 9;

    // Diff has no way to strip trailing spaces, so we do preprocessing for it.
    File f1copy = new File(TestUtils.getBaseTmpDir(), "f1_" + f1.getName());
    File f2copy = new File(TestUtils.getBaseTmpDir(), "f2_" + f2.getName());
    FileUtils.writeLines(f1copy, lines1);
    FileUtils.writeLines(f2copy, lines2);

    String diffCmd = String.format("sdiff --width=%d '%s' '%s'", diffWidth, f1copy, f2copy);
    CommandResult commandResult = CommandUtil.runShellCommand(diffCmd);
    List<String> stdoutLines = commandResult.getStdoutLines();

    List<String> diffLines = new ArrayList<>();

    int maxDiffLineLength = 0;
    for (String line : stdoutLines) {
      String expandedLine = StringUtil.expandTabs(line);
      diffLines.add(expandedLine);
      maxDiffLineLength = Math.max(maxDiffLineLength, expandedLine.length());
    }

    int rhsColumnOffset = -1;

    // Our previous attempts to devise middle column and offset by bruteforce from the middle
    // had proven to be quite inconsistent between platforms/diff versions.
    // Instead, let's try to use the first matching row of a right-hand side and work from there.
    outer:
    for (int i = 0; i < diffLines.size(); ++i) {
      String diffLine = diffLines.get(i);

      // "<" might mean that this is unique to a left-hand side.
      // (Could also be used for other purposes, but we have plenty of lines.)
      if (diffLine.matches("\\s*") || diffLine.contains("<"))
        continue;

      for (int j = i; j >= 0; --j) {
        String rhsLine = lines2.get(j);

        if (rhsLine.matches("\\s*")) {
          continue;
        }

        List<Integer> possibleRhsOffsets = new ArrayList<>();
        int offset = -1;
        do {
          offset = diffLine.indexOf(rhsLine, offset + 1);
          if (offset > -1) {
            possibleRhsOffsets.add(offset);
          }
        } while (offset != -1);

        // If there are too many possible offsets, it becomes ambiguous and we search
        // for a better line.
        if (!possibleRhsOffsets.isEmpty() && possibleRhsOffsets.size() <= 2) {
          rhsColumnOffset = possibleRhsOffsets.get(possibleRhsOffsets.size() - 1);
          break outer;
        }
      }
    }

    if (rhsColumnOffset == -1) {
      LOG.error("Side-by-side diff raw output with tabs expanded:\n" +
          StringUtil.expandTabsAndConcatenate(diffLines));
      throw new IOException(
          "Was not able to find the right-hand side offset of the side-by-side diff. " +
              "See the raw output with tabs expanded in the log.");
    }

    List<Set<Character>> columnChars = getColumnCharacters(diffLines, maxDiffLineLength);

    // Zero-based column of the middle of the side-by-side diff output as produced by the diff
    // command.
    // This column contains mismatch character markers.
    int midColumn = -1;

    for (int i = rhsColumnOffset - 2; i > 0; --i) {
      Set<Character> midSet = columnChars.get(i);
      if (VALID_MID_CHAR_SUPERSET.containsAll(midSet)) {
        if (!VALID_LEFT_RIGHT_CHAR_SUPERSET.containsAll(midSet)) {
          // We definitely found a middle column
          midColumn = i;
          break;
        }
        // Otherwise, could either be middle column of a no-mismatch diff,
        // or column added as padding.
      } else {
        // Found a data column, we've looked too far.
        // This means there are no mismatches and we can treat ANY space-only
        // column as a middle column.
        midColumn = rhsColumnOffset - 1;
        assertTrue(VALID_LEFT_RIGHT_CHAR_SUPERSET.containsAll(columnChars.get(midColumn)));
      }
    }

    int lLineIdx = 0;
    int rLineIdx = 0;
    List<SideBySideDiffLine> sxsLines = new ArrayList<>();

    for (String diffLine : diffLines) {
      String lStr =
          StringUtil.rtrim(diffLine.substring(0, Math.min(midColumn - 1, diffLine.length())));
      String rStr =
          diffLine.length() > rhsColumnOffset
              ? StringUtil.rtrim(diffLine.substring(rhsColumnOffset))
              : "";

      char diffChar = midColumn < diffLine.length() ? diffLine.charAt(midColumn) : ' ';

      sxsLines.add(new SideBySideDiffLine(lLineIdx, lStr, rLineIdx, rStr, diffChar));
      if (diffChar != '>') {
        // ">" would mean this line is unique to the right-hand-side file.
        sanityCheckLinesMatch("lhs", lines1, lLineIdx, lStr);
        lLineIdx++;
      }
      if (diffChar != '<') {
        // "<" would mean this line is unique to the left-hand-side file.
        sanityCheckLinesMatch("rhs", lines2, rLineIdx, rStr);
        rLineIdx++;
      }
    }

    int valueWidth1 = 0;
    for (SideBySideDiffLine sxsLine : sxsLines) {
      valueWidth1 = Math.max(valueWidth1, sxsLine.s1.length());
    }
    int lineNumWidth1 = String.valueOf(lines1.size()).length();
    int lineNumWidth2 = String.valueOf(lines2.size()).length();
    String fmt = "%" + lineNumWidth1 + "s  " +
        "%-" + valueWidth1 + "s  %c  %" + lineNumWidth2 + "s  %s";
    for (SideBySideDiffLine diffLine: sxsLines) {
      result.append(StringUtil.rtrim(
          String.format(fmt, diffLine.getLeftLineNumStr(), diffLine.s1, diffLine.diffChar,
              diffLine.getRightLineNumStr(), diffLine.s2)));
      result.append('\n');
    }

    return result.toString();
  }

}

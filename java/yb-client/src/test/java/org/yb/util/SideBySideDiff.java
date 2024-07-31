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

import java.io.File;
import java.io.IOException;
import java.util.*;

/**
 * Produces a human-readable side-by-side diff between two files.
 */
public class SideBySideDiff {

  public static String generate(File f1, File f2) throws IOException {
    // There used to be logic here that tried to ensure no clipping of the left or right sides, and
    // added line numbers to each side by doing postprocessing of the diff tool's output. The logic
    // became very complicated, and would have required enhancement to tab handling for characters
    // that display in two columns. To reduce the maintenance burden, we now pick 157 columns as the
    // width and simply use the raw output of sdiff. This will give either side of the diff around
    // 77 characters of width. For realistic files, this will generally lead to clipping on at least
    // one side, so we can show a side-by-side diff, which many developers find useful, but without
    // any confusion caused by clipping that happens only rarely. Why the odd numbers though? If
    // you're in a terminal with 8-column tab stops, this causes the RHS to begin at a tab stop
    // which will make the LHS and the RHS identical when the input files have embedded tabs.
    // Embedded tabs aren't expanded even with the --expand-tabs flag.
    String diffCmd = String.format("sdiff -Z --width=157 --expand-tabs '%s' '%s'", f1, f2);
    CommandResult commandResult = CommandUtil.runShellCommand(diffCmd);
    List<String> stdoutLines = commandResult.getStdoutLines();
    return StringUtil.joinLinesForLoggingNoPrefix(stdoutLines);
  }
}

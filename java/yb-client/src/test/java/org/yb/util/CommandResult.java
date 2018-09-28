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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

public class CommandResult {
  private static final Logger LOG = LoggerFactory.getLogger(CommandResult.class);

  public final String cmd;
  public final int exitCode;
  public final List<String> stdoutLines;
  public final List<String> stderrLines;

  public CommandResult(
      String cmd, int exitCode, List<String> stdoutLines, List<String> stderrLines) {
    this.cmd = cmd;
    this.exitCode = exitCode;
    this.stdoutLines = stdoutLines;
    this.stderrLines = stderrLines;
  }

  public boolean isSuccess() {
    return exitCode == 0;
  }

  public void logErrorOutput() {
    if (!stderrLines.isEmpty()) {
      LOG.warn("Standard error output from command {{ " + cmd + " }}" +
          (exitCode == 0 ? "" : " (exit code: " + exitCode + "):\n" +
              StringUtil.joinLinesForLogging(stderrLines)));
    }
  }

}

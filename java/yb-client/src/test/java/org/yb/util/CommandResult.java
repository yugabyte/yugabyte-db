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

  private final String cmd;
  private final int exitCode;
  private final List<String> stdoutLines;
  private final List<String> stderrLines;

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

  private String commandLineForLog() {
    return "{{ " + cmd + " }}";
  }

  private String commandLineAndExitCodeForLog() {
    return "command " + commandLineForLog() + (
        exitCode == 0 ? "" : " (exit code: " + exitCode + ")");
  }

  private enum StandardStreamType {
    STDOUT,
    STDERR
  }

  private boolean logStandardStream(StandardStreamType streamType, List<String> lines) {
    if (lines.isEmpty()) {
      return false;
    }

    LOG.warn("Standard " + streamType.toString().toLowerCase() + " from " +
        commandLineAndExitCodeForLog() + ":\n" +
            StringUtil.joinLinesForLogging(
                streamType == StandardStreamType.STDOUT ? stdoutLines : stderrLines));
    return true;
  }

  public boolean logStderr() {
    return logStandardStream(StandardStreamType.STDERR, stderrLines);
  }

  public boolean logStdout() {
    return logStandardStream(StandardStreamType.STDOUT, stdoutLines);
  }

  public void logOutput() {
    boolean exitCodeLogged = false;
    if (logStdout()) {
      exitCodeLogged = true;
    }
    if (logStderr()) {
      exitCodeLogged = true;
    }
    if (!exitCodeLogged && exitCode != 0) {
      LOG.warn("Non-zero exit code from " + commandLineAndExitCodeForLog());
    }
  }

  public List<String> getStdoutLines() {
    return stdoutLines;
  }

  public List<String> getStderrLines() {
    return stderrLines;
  }

  @Override
  public String toString() {
    return "Result of running command {{ " + cmd + " }}: exit code " + exitCode;
  }

}

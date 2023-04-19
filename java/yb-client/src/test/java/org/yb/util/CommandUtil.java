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
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;

public final class CommandUtil {

  private CommandUtil() {
  }

  private static final Logger LOG = LoggerFactory.getLogger(CommandUtil.class);

  public static CommandResult runShellCommand(String cmd) throws IOException {
    File outputFile = new File(TestUtils.getBaseTmpDir() + "/tmp_stdout_"  +
        RandomUtil.randomNonNegNumber() + ".txt");
    File errorFile = new File(TestUtils.getBaseTmpDir() + "/tmp_stderr_"  +
        RandomUtil.randomNonNegNumber() + ".txt");
    try {

      Process process = new ProcessBuilder().command(Arrays.asList(new String[]{
          "bash", "-c", cmd
      })).redirectOutput(outputFile).redirectError(errorFile).start();
      int exitCode;
      try {
        exitCode = process.waitFor();
      } catch (InterruptedException ex) {
        throw new IOException("Interrupted while trying to run command: " + cmd, ex);
      }
      return new CommandResult(
          cmd,
          exitCode,
          FileUtil.readLinesFrom(outputFile),
          FileUtil.readLinesFrom(errorFile));
    } catch (IOException ex) {
      LOG.error("Exception while running command: " + cmd, ex);
      throw ex;
    } finally {
      if (outputFile.exists()) {
        outputFile.delete();
      }
      if (errorFile.exists()) {
        errorFile.delete();
      }
    }
  }

  public static List<String> flagsToArgs(Map<String, String> flags) {
    return flags.entrySet().stream()
        .map(e -> "--" + e.getKey() + "=" + e.getValue())
        .collect(Collectors.toList());
  }
}

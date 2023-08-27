// Copyright (c) Yugabyte, Inc.
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
package org.yb.pgsql;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.BaseYBTest;
import org.yb.minicluster.LogPrinter;
import org.yb.YBTestRunner;

/**
 * Lint all YB regress schedules under src/test/regress.
 *
 * For details on exit codes, please refer to yb_lint_regress_schedule.sh.
 */
@RunWith(value=YBTestRunner.class)
public class TestLintPgRegressSchedules extends BaseYBTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestLintPgRegressSchedules.class);

  public static final File PG_REGRESS_DIR = PgRegressBuilder.PG_REGRESS_DIR;
  public static final File LINT_EXECUTABLE = new File(PG_REGRESS_DIR,
                                                      "yb_lint_regress_schedule.sh");

  @Override
  public int getTestMethodTimeoutSec() {
    return 30;
  }

  @Test
  public void lintSchedules() throws Exception {
    Pattern scheduleRe = Pattern.compile("^yb_\\w+_schedule$");

    // Lint each schedule.
    for (File f : PG_REGRESS_DIR.listFiles(File::isFile)) {
      String filename = f.getName();
      Matcher matcher = scheduleRe.matcher(filename);
      if (matcher.find()) {
        lintSchedule(filename);
      }
    }
  }

  private void lintSchedule(String schedule) throws Exception {
    LOG.info("Linting {}", schedule);

    // Build process.
    List<String> args = Arrays.asList(
        LINT_EXECUTABLE.toString(),
        schedule);
    ProcessBuilder procBuilder = new ProcessBuilder(args);
    procBuilder.directory(PG_REGRESS_DIR);

    // Start process.
    Process proc = procBuilder.start();

    // Check results.
    int exitCode = proc.waitFor();
    if (exitCode != 0) {
      BufferedReader reader = new BufferedReader(new InputStreamReader(proc.getInputStream()));
      String line;
      String logPrefix = "linter|";

      // Log output.
      while ((line = reader.readLine()) != null) {
        System.out.println(logPrefix + line);
      }
      reader = new BufferedReader(new InputStreamReader(proc.getErrorStream()));
      while ((line = reader.readLine()) != null) {
        System.out.println(logPrefix + line);
      }
      System.out.flush();

      // Throw error.
      throw new AssertionError(String.format("linter on %s exited with error code: %d",
                                             schedule, exitCode));
    }
  }
}

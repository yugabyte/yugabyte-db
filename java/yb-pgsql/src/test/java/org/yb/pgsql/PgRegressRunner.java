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
package org.yb.pgsql;

import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.ExternalDaemonLogErrorListener;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.LogPrinter;
import org.yb.util.*;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.*;
import java.util.concurrent.ConcurrentSkipListSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A wrapper for running the pg_regress utility.
 */
public class PgRegressRunner {

  private static final Logger LOG = LoggerFactory.getLogger(PgRegressRunner.class);

  private Set<String> failedTests = new ConcurrentSkipListSet<>();

  private File pgRegressInputDir;
  private File pgRegressOutputDir;
  private File diffsFilePath;
  private String label;
  private long maxRuntimeMillis;

  public PgRegressRunner(File pgRegressInputDir, String schedule, long maxRuntimeMillis) {
    this.pgRegressInputDir = pgRegressInputDir;

    this.label = String.format("using schedule %s at %s", schedule, pgRegressInputDir);
    this.maxRuntimeMillis = maxRuntimeMillis;

    File testDir = new File(TestUtils.getBaseTmpDir(), "pgregress_output");
    this.pgRegressOutputDir = new File(testDir, schedule);
    this.diffsFilePath = new File(pgRegressOutputDir, "regression.diffs");
  }

  /**
   * Failed test line example:
   *
   * <pre>
   * test yb_tablegroup                ... FAILED
   * test yb_tablegroup_dml            ... FAILED (test process exited with exit code 2)
   * </pre>
   *
   * (We don't care about the optional exit code suffix.)
   */
  private final Pattern failedTestLineRe =
      Pattern.compile("^test\\s+([a-zA-Z0-9_-]+)\\s+[.]+\\s+FAILED");

  private LogErrorListener createLogErrorListener(int pid) {
    return new ExternalDaemonLogErrorListener("pg_regress with pid " + pid) {
      @Override
      public void handleLine(String line) {
        super.handleLine(line);
        Matcher matcher = failedTestLineRe.matcher(line);
        if (matcher.find()) {
          failedTests.add(matcher.group(1));
        }
      }
    };
  }

  public File outputDir() {
    return pgRegressOutputDir;
  }

  public void run(ProcessBuilder procBuilder) throws Exception {
    if (diffsFilePath.exists()) {
      diffsFilePath.delete();
    }

    long    startTimeMillis = System.currentTimeMillis();
    Process pgRegressProc   = procBuilder.start();
    int     pgRegressPid    = ProcessUtil.pidOfProcess(pgRegressProc);
    String  logPrefix       = "pg_regress|pid" + pgRegressPid;
    int     exitCode        = -1;
    long    runtimeMillis;

    try (LogPrinter stdoutLogPrinter =
             new LogPrinter(pgRegressProc.getInputStream(), logPrefix + "|stdout ",
                 createLogErrorListener(pgRegressPid));
         LogPrinter stderrLogPrinter =
             new LogPrinter(pgRegressProc.getErrorStream(), logPrefix + "|stderr ",
                 createLogErrorListener(pgRegressPid))) {

      exitCode = pgRegressProc.waitFor();
      runtimeMillis = System.currentTimeMillis() - startTimeMillis;
    } finally {
      if (diffsFilePath.exists()) {
        List<String> diffsLines = FileUtil.readLinesFrom(diffsFilePath);
        String diffsContent = StringUtils.join(diffsLines.iterator(), "\n");
        LOG.warn("Contents of {}:\n{}", diffsFilePath, diffsContent);
      } else if (exitCode != 0) {
        LOG.error("File does not exist: {}", diffsFilePath);
      }
    }

    Set<String> sortedFailedTests = new TreeSet<>();
    sortedFailedTests.addAll(failedTests);
    if (!sortedFailedTests.isEmpty()) {
      LOG.warn("Failed tests: {}", sortedFailedTests);
      for (String testName : sortedFailedTests) {
        File expectedFile = new File(new File(pgRegressOutputDir, "expected"), testName + ".out");
        File resultFile = new File(new File(pgRegressOutputDir, "results"), testName + ".out");
        if (!expectedFile.exists()) {
          LOG.warn("Expected test output file {} not found.", expectedFile);
          continue;
        }
        if (!resultFile.exists()) {
          LOG.warn("Actual test output file {} not found.", resultFile);
          continue;
        }
        LOG.warn("Side-by-side diff between expected output and actual output of {}:\n{}",
            testName, SideBySideDiff.generate(expectedFile, resultFile));
      }
    } else if (exitCode != 0) {
      LOG.error("No failed tests detected!");
    }

    if (!ConfForTesting.isCI()) {
      final Path pgRegressOutputPath = Paths.get(pgRegressOutputDir.toString());

      LOG.info("Copying test result files and generated SQL and expected output {} back to {}",
          pgRegressOutputPath, pgRegressInputDir);
      Files.find(
          pgRegressOutputPath,
          Integer.MAX_VALUE,
          (filePath, fileAttr) -> fileAttr.isRegularFile()
      ).forEach(pathToCopy -> {
        String fileName = pathToCopy.toFile().getName();
        String relPathStr = pgRegressOutputPath.relativize(pathToCopy).toString();
        if ((fileName.endsWith(".out") || fileName.endsWith(".diffs")) &&
            !relPathStr.startsWith("expected/")) {
          File srcFile = pathToCopy.toFile();
          File destFile = new File(pgRegressInputDir, relPathStr);
          LOG.info("Copying file {} to {}", srcFile, destFile);
          try {
            FileUtils.copyFile(srcFile, destFile);
          } catch (IOException ex) {
            LOG.error("Failed copying file " + srcFile + " to " + destFile, ex);
          }
        }
      });
    }

    if (EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_REGRESS_IGNORE_RESULT")) {
      return;
    }

    if (exitCode != 0) {
      throw new AssertionError("pg_regress exited with error code: " + exitCode +
          ", failed tests: " + sortedFailedTests);
    }
    if (!sortedFailedTests.isEmpty()) {
      throw new AssertionError("Tests failed (but pg_regress exit code is 0, unexpectedly): " +
          sortedFailedTests);
    }
    if (maxRuntimeMillis != 0 && runtimeMillis > maxRuntimeMillis) {
      throw new AssertionError("pg_regress (" + label + ") exceeded max runtime. " +
                               "Elapsed time = " + runtimeMillis + " msecs. " +
                               "Max time = " + maxRuntimeMillis + " msecs.");
    }

    LOG.info("Completed pg_regress ({}). Elapsed time = {} msecs", label, runtimeMillis);
  }
}

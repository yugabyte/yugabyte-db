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
import org.apache.commons.io.FilenameUtils;
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
import java.util.stream.Collectors;

/**
 * A wrapper for running the pg_regress utility.
 */
public class PgRegressRunner {

  private static final Logger LOG = LoggerFactory.getLogger(PgRegressRunner.class);

  private File pgRegressInputDir;
  private File pgRegressOutputDir;
  private Process pgRegressProc;
  private LogPrinter stdoutLogPrinter, stderrLogPrinter;
  private String label;
  private File regressionDiffsPath;

  private int exitCode = -1;
  private int pgRegressPid;

  private Set<String> failedTests = new ConcurrentSkipListSet<>();

  private long maxRuntimeMillis;
  private long startTimeMillis;

  public PgRegressRunner(File pgRegressInputDir, String schedule, long maxRuntimeMillis) {
    this.pgRegressInputDir = pgRegressInputDir;

    this.label = String.format("using schedule %s at %s", schedule, pgRegressInputDir);
    this.maxRuntimeMillis = maxRuntimeMillis;

    File testDir = new File(TestUtils.getBaseTmpDir(), "pgregress_output");
    pgRegressOutputDir = new File(testDir, schedule);
    regressionDiffsPath = new File(pgRegressOutputDir, "regression.diffs");
  }

  private Pattern FAILED_TEST_LINE_RE =
      Pattern.compile("^test\\s+([a-zA-Z0-9_-]+)\\s+[.]+\\s+FAILED\\s*$");

  private LogErrorListener createLogErrorListener() {
    return new ExternalDaemonLogErrorListener("pg_regress with pid " + pgRegressPid) {
      @Override
      public void handleLine(String line) {
        super.handleLine(line);
        Matcher matcher = FAILED_TEST_LINE_RE.matcher(line);
        if (matcher.matches()) {
          failedTests.add(matcher.group(1));
        }
      }
    };
  }

  public File outputDir() {
    return pgRegressOutputDir;
  }

  public void start(ProcessBuilder procBuilder)
        throws IOException, NoSuchFieldException, IllegalAccessException {
    if (regressionDiffsPath.exists()) {
      regressionDiffsPath.delete();
    }

    startTimeMillis = System.currentTimeMillis();
    pgRegressProc = procBuilder.start();
    pgRegressPid = ProcessUtil.pidOfProcess(pgRegressProc);
    String logPrefix = "pg_regress|pid" + pgRegressPid;
    stdoutLogPrinter = new LogPrinter(
        pgRegressProc.getInputStream(),
        logPrefix + "|stdout ",
        createLogErrorListener());
    stderrLogPrinter = new LogPrinter(
        pgRegressProc.getErrorStream(),
        logPrefix + "|stderr ",
        createLogErrorListener());
  }

  public void stop() throws InterruptedException, IOException {
    exitCode = pgRegressProc.waitFor();
    long runtimeMillis = System.currentTimeMillis() - startTimeMillis;
    stdoutLogPrinter.stop();
    stderrLogPrinter.stop();
    if (regressionDiffsPath.exists()) {
      BufferedReader reader = new BufferedReader(new InputStreamReader(
          new FileInputStream(regressionDiffsPath.getPath())));
      String line;
      StringBuilder diffs = new StringBuilder();
      while ((line = reader.readLine()) != null) {
        diffs.append(line);
        diffs.append("\n");
      }
      LOG.warn("Contents of " + regressionDiffsPath + ":\n" + diffs);
    } else if (exitCode != 0) {
      LOG.info("File does not exist: " + regressionDiffsPath);
    }

    Set<String> sortedFailedTests = new TreeSet<>();
    sortedFailedTests.addAll(failedTests);
    if (!sortedFailedTests.isEmpty()) {
      LOG.info("Failed tests: " + sortedFailedTests);
      for (String testName : sortedFailedTests) {
        File expectedFile = new File(new File(pgRegressOutputDir, "expected"), testName + ".out");
        File resultFile = new File(new File(pgRegressOutputDir, "results"), testName + ".out");
        if (!expectedFile.exists()) {
          LOG.warn("Expected test output file " + expectedFile + " not found.");
          continue;
        }
        if (!resultFile.exists()) {
          LOG.warn("Actual test output file " + resultFile + " not found.");
          continue;
        }
        LOG.warn("Side-by-side diff between expected output and actual output:\n" +
            new SideBySideDiff(expectedFile, resultFile).getSideBySideDiff());
      }
    }

    if (!ConfForTesting.isCI()) {
      final Path pgRegressOutputPath = Paths.get(pgRegressOutputDir.toString());

      LOG.info("Copying test result files and generated SQL and expected output " +
               pgRegressOutputPath + " back to " + pgRegressInputDir);
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
          LOG.info("Copying file " + srcFile + " to " + destFile);
          try {
            FileUtils.copyFile(srcFile, destFile);
          } catch (IOException ex) {
            LOG.warn("Failed copying file " + srcFile + " to " + destFile, ex);
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

    LOG.info(String.format("Completed pg_regress (%s). Elapsed time = %d msecs",
                           label, runtimeMillis));
  }

}

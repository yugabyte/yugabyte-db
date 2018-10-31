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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.ExternalDaemonLogErrorListener;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.LogPrinter;
import org.yb.util.EnvAndSysPropertyUtil;
import org.yb.util.ProcessUtil;

import java.io.*;
import java.util.HashMap;
import java.util.Map;

/**
 * A wrapper for running the pg_regress utility.
 */
public class PgRegressRunner {

  private static final Logger LOG = LoggerFactory.getLogger(PgRegressRunner.class);

  private File pgBinDir;
  private File pgRegressDir;
  private File pgRegressExecutable;
  private Process pgRegressProc;
  private LogPrinter stdoutLogPrinter, stderrLogPrinter;
  private String pgHost;
  private int pgPort;
  private String pgUser;
  private File regressionDiffsPath;

  private int exitCode = -1;
  private Map<String, String> extraEnvVars = new HashMap<>();
  private int pgRegressPid;

  public PgRegressRunner(String pgHost, int pgPort, String pgUser) {
    pgRegressDir = new File(TestUtils.getBuildRootDir(), "postgres_build/src/test/regress");
    pgBinDir = new File(TestUtils.getBuildRootDir(), "postgres/bin");
    pgRegressExecutable = new File(pgRegressDir, "pg_regress");
    this.pgHost = pgHost;
    this.pgPort = pgPort;
    this.pgUser = pgUser;
    regressionDiffsPath = new File(pgRegressDir, "regression.diffs");
  }

  public void setEnvVars(Map<String, String> envVars) {
    extraEnvVars = envVars;
  }

  private LogErrorListener createLogErrorListener() {
    return new ExternalDaemonLogErrorListener("pg_regress with pid " + pgRegressPid);
  }

  public void start() throws IOException, NoSuchFieldException, IllegalAccessException {
    if (regressionDiffsPath.exists()) {
      regressionDiffsPath.delete();
    }
    ProcessBuilder procBuilder =
        new ProcessBuilder(
            pgRegressExecutable.toString(),
            "--inputdir=" + pgRegressDir,
            "--bindir=" + pgBinDir,
            "--dlpath=" + pgRegressDir,
            "--port=" + pgPort,
            "--host=" + pgHost,
            "--user=" + pgUser,
            "--schedule=yb_serial_schedule");
    procBuilder.directory(pgRegressDir);
    procBuilder.environment().putAll(extraEnvVars);

    File postprocessScript = new File(
        TestUtils.findYbRootDir() + "/build-support/pg_regress_postprocess_output.py");

    if (!postprocessScript.exists()) {
      throw new IOException("File does not exist: " + postprocessScript);
    }
    if (!postprocessScript.canExecute()) {
      throw new IOException("Not executable: " + postprocessScript);
    }

    // Ask pg_regress to run a post-processing script on the output to remove some sanitizer
    // suppressions before running the diff command, and also to remove trailing whitespace.
    procBuilder.environment().put("YB_PG_REGRESS_RESULTSFILE_POSTPROCESS_CMD",
        postprocessScript.toString());

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

    if (exitCode != 0 &&
        !EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_REGRESS_IGNORE_RESULT")) {
      throw new AssertionError("pg_regress exited with error code: " + exitCode);
    }
  }

}

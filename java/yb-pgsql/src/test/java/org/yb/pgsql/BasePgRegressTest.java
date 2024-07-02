// Copyright (c) YugabyteDB, Inc.
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

import java.io.File;
import java.util.Map;
import java.util.TreeMap;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class BasePgRegressTest extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(BasePgRegressTest.class);

  // Postgres flags.
  private static final String MASTERS_FLAG = "FLAGS_pggate_master_addresses";
  private static final String YB_ENABLED_IN_PG_ENV_VAR_NAME = "YB_ENABLED_IN_POSTGRES";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "compute_query_id=regress");
    return flagMap;
  }

  public void runPgRegressTest(
      File inputDir, String schedule, long maxRuntimeMillis, File executable) throws Exception {
    final int tserverIndex = 0;
    PgRegressRunner pgRegress = new PgRegressRunner(inputDir, schedule, maxRuntimeMillis);
    ProcessBuilder procBuilder = new PgRegressBuilder(executable)
        .setDirs(inputDir, pgRegress.outputDir())
        .setSchedule(schedule)
        .setHost(getPgHost(tserverIndex))
        .setPort(getPgPort(tserverIndex))
        .setUser(DEFAULT_PG_USER)
        .setDatabase("yugabyte")
        .setEnvVars(getPgRegressEnvVars())
        .getProcessBuilder();
    pgRegress.run(procBuilder);
  }

  public void runPgRegressTest(File inputDir, String schedule) throws Exception {
    runPgRegressTest(
        inputDir, schedule, 0 /* maxRuntimeMillis */,
        PgRegressBuilder.PG_REGRESS_EXECUTABLE);
  }

  public void runPgRegressTest(String schedule, long maxRuntimeMillis) throws Exception {
    runPgRegressTest(
        PgRegressBuilder.PG_REGRESS_DIR /* inputDir */, schedule, maxRuntimeMillis,
        PgRegressBuilder.PG_REGRESS_EXECUTABLE);
  }

  public void runPgRegressTest(String schedule) throws Exception {
    runPgRegressTest(schedule, 0 /* maxRuntimeMillis */);
  }

  protected Map<String, String> getPgRegressEnvVars() {
    Map<String, String> pgRegressEnvVars = new TreeMap<>();
    pgRegressEnvVars.put(MASTERS_FLAG, masterAddresses);
    pgRegressEnvVars.put(YB_ENABLED_IN_PG_ENV_VAR_NAME, "1");

    for (Map.Entry<String, String> entry : System.getenv().entrySet()) {
      String envVarName = entry.getKey();
      if (envVarName.startsWith("postgres_FLAGS_")) {
        String downstreamEnvVarName = envVarName.substring(9);
        LOG.info("Found env var " + envVarName + ", setting " + downstreamEnvVarName + " for " +
                 "pg_regress to " + entry.getValue());
        pgRegressEnvVars.put(downstreamEnvVarName, entry.getValue());
      }
    }

    // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
    pgRegressEnvVars.put("YB_PG_FALLBACK_SYSTEM_USER_NAME", "yugabyte");

    return pgRegressEnvVars;
  }
}

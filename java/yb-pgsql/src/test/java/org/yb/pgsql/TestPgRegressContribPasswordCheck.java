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

import static org.yb.AssertionWrappers.*;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.ProcessUtil;
import org.yb.YBTestRunner;

import java.io.File;
import java.sql.Statement;
import java.util.Arrays;

@RunWith(value = YBTestRunner.class)
public class TestPgRegressContribPasswordCheck extends BasePgRegressTest {
    @Override
    public int getTestMethodTimeoutSec() {
        return 1800;
    }

    // each test in yb_schedule installs passwordcheck with the LOAD command

    @Test
    public void schedule() throws Exception {
        runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                "postgres_build/contrib/passwordcheck"),
                "yb_schedule");
    }

    /**
     * A ysql_dumpall script sets each role's password to its stored SCRAM secret. Restoring the
     * script in a session that loaded passwordcheck must bring the role back unchanged.
     */
    @Test
    public void restoreScramPasswordFromDump() throws Exception {
        final String role = "regress_dump_role";
        final String roleQuery = "SELECT rolcanlogin, rolconnlimit, rolpassword FROM pg_authid" +
                " WHERE rolname = '" + role + "'";
        Row dumped;
        try (Statement stmt = connection.createStatement()) {
            stmt.execute("SET password_encryption = 'scram-sha-256'");
            stmt.execute("CREATE ROLE " + role + " LOGIN CONNECTION LIMIT 3" +
                    " PASSWORD 'Dump_restore_1'");
            dumped = getSingleRow(stmt, roleQuery);
        }
        assertTrue(dumped.getString(2).startsWith("SCRAM-SHA-256$"));

        File pgBinDir = PgRegressBuilder.getPgBinDir();
        String host = getPgHost(0);
        String port = Integer.toString(getPgPort(0));
        String dumpFile = getAbsFilePath("passwordcheck_roles_dump.sql");
        ProcessUtil.executeSimple(Arrays.asList(
                new File(pgBinDir, "ysql_dumpall").toString(),
                "-h", host, "-p", port, "-U", DEFAULT_PG_USER,
                "--roles-only", "-f", dumpFile), "ysql_dumpall");

        try (Statement stmt = connection.createStatement()) {
            stmt.execute("DROP ROLE " + role);
        }

        // The script also has the roles that still exist, such as yugabyte. Their statements
        // fail, and ysqlsh goes on.
        ProcessUtil.executeSimple(Arrays.asList(
                new File(pgBinDir, "ysqlsh").toString(),
                "-h", host, "-p", port, "-U", DEFAULT_PG_USER,
                "-c", "LOAD 'passwordcheck'", "-f", dumpFile), "ysqlsh");

        try (Statement stmt = connection.createStatement()) {
            assertRow(dumped, getSingleRow(stmt, roleQuery));
        }
    }
}

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

import org.apache.commons.io.IOUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.ProcessUtil;
import org.yb.YBTestRunner;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

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
     * A backup's ysql_dumpall script sets each role's password to its stored SCRAM secret.
     * Restoring the script in a session that loaded passwordcheck must bring the role back
     * unchanged.
     */
    @Test
    public void restoreScramPasswordFromDump() throws Exception {
        final String role = "regress_dump_role";
        final String roleQuery = "SELECT rolcanlogin, rolconnlimit, rolpassword FROM pg_authid" +
                " WHERE rolname = '" + role + "'";
        Row dumped;
        try (Statement stmt = connection.createStatement()) {
            stmt.execute("SET password_encryption = 'scram-sha-256'");
            // The script also sets yugabyte's password, and passwordcheck refuses the default,
            // yugabyte (see restoreRoleNamePasswordFromDump).
            stmt.execute("ALTER ROLE yugabyte PASSWORD 'Dump_restore_2'");
            stmt.execute("CREATE ROLE " + role + " LOGIN CONNECTION LIMIT 3" +
                    " PASSWORD 'Dump_restore_1'");
            dumped = getSingleRow(stmt, roleQuery);
        }
        assertTrue(dumped.getString(2).startsWith("SCRAM-SHA-256$"));

        String dumpFile = dumpRoles("passwordcheck_roles_dump.sql");
        try (Statement stmt = connection.createStatement()) {
            stmt.execute("DROP ROLE " + role);
        }
        restoreRoles(dumpFile, true /* loadPasswordcheck */, 0 /* expectedExitCode */);

        try (Statement stmt = connection.createStatement()) {
            assertRow(dumped, getSingleRow(stmt, roleQuery));
        }
    }

    /**
     * passwordcheck refuses a password equal to the role name, also a pre-hashed one, so a
     * restore with passwordcheck loaded fails when a role has such a password. The workaround is
     * to restore without passwordcheck.
     * The test checks both: the restore that fails, then the workaround that succeeds.
     */
    @Test
    public void restoreRoleNamePasswordFromDump() throws Exception {
        final String role = "regress_dump_name_role";
        final String rolesQuery = "SELECT rolname, rolcanlogin, rolpassword FROM pg_authid" +
                " WHERE rolname IN ('" + role + "', 'yugabyte') ORDER BY rolname";
        List<Row> dumped;
        try (Statement stmt = connection.createStatement()) {
            // This session does not load passwordcheck, so it accepts these passwords.
            stmt.execute("ALTER ROLE yugabyte PASSWORD 'yugabyte'");
            stmt.execute("CREATE ROLE " + role + " LOGIN PASSWORD '" + role + "'");
            dumped = getRowList(stmt, rolesQuery);
        }

        String dumpFile = dumpRoles("passwordcheck_name_roles_dump.sql");
        try (Statement stmt = connection.createStatement()) {
            stmt.execute("DROP ROLE " + role);
        }

        // ysqlsh exits with 3 when ON_ERROR_STOP stops the script.
        String output = restoreRoles(dumpFile, true /* loadPasswordcheck */, 3);
        assertTrue(output, output.contains("ERROR:  password must not contain user name"));
        try (Statement stmt = connection.createStatement()) {
            // The restore stopped at the role's ALTER ROLE, after its CREATE ROLE, so the role is
            // left without LOGIN or a password. A rerun skips roles that exist, so drop it first.
            assertRow(new Row(false, null), getSingleRow(stmt,
                    "SELECT rolcanlogin, rolpassword FROM pg_authid WHERE rolname = '" + role +
                    "'"));
            stmt.execute("DROP ROLE " + role);
        }

        restoreRoles(dumpFile, false /* loadPasswordcheck */, 0 /* expectedExitCode */);
        try (Statement stmt = connection.createStatement()) {
            assertRows(dumped, getRowList(stmt, rolesQuery));
            // Don't leave this role behind for the other tests.
            stmt.execute("DROP ROLE " + role);
        }
    }

    /** Dumps the roles the way a YugabyteDB Anywhere backup does. */
    private String dumpRoles(String fileName) throws Exception {
        String dumpFile = getAbsFilePath(fileName);
        ProcessUtil.executeSimple(Arrays.asList(
                new File(PgRegressBuilder.getPgBinDir(), "ysql_dumpall").toString(),
                "-h", getPgHost(0), "-p", Integer.toString(getPgPort(0)), "-U", DEFAULT_PG_USER,
                "--roles-only", "--include-yb-metadata", "--dump-role-checks", "-f", dumpFile),
                "ysql_dumpall");
        return dumpFile;
    }

    /**
     * Runs a roles dump the way a YugabyteDB Anywhere restore does: ON_ERROR_STOP stops at the
     * first error, and ignore_existing_roles skips the roles that already exist. Returns the
     * output.
     */
    private String restoreRoles(String dumpFile, boolean loadPasswordcheck, int expectedExitCode)
            throws Exception {
        List<String> args = new ArrayList<>(Arrays.asList(
                new File(PgRegressBuilder.getPgBinDir(), "ysqlsh").toString(),
                "-h", getPgHost(0), "-p", Integer.toString(getPgPort(0)), "-U", DEFAULT_PG_USER,
                "-v", "ON_ERROR_STOP=1", "-v", "ignore_existing_roles=1"));
        if (loadPasswordcheck) {
            args.addAll(Arrays.asList("-c", "LOAD 'passwordcheck'"));
        }
        args.addAll(Arrays.asList("-f", dumpFile));
        Process process = new ProcessBuilder(args).redirectErrorStream(true).start();
        String output = IOUtils.toString(process.getInputStream(), StandardCharsets.UTF_8);
        assertEquals(output, expectedExitCode, process.waitFor());
        return output;
    }
}

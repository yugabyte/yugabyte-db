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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import com.yugabyte.util.PSQLException;

// Runs the pg_regress password-history test suite on YB code.
@RunWith(value = YBTestRunner.class)
public class TestPgRegressPasswordHistory extends BasePgRegressTest {
    @Override
    protected Map<String, String> getTServerFlags() {
        Map<String, String> flagMap = super.getTServerFlags();
        appendToYsqlPgConf(flagMap, "yb_allow_prehashed_passwords=false");
        appendToYsqlPgConf(flagMap, "yb_password_reuse_history=3");
        // phtxn rolls back ALTER ROLE. Debug builds commit that DDL on its own
        // unless transactional DDL is on. Release already defaults it on.
        toggleDDLMode(flagMap, /* useLegacy */ false);
        return flagMap;
    }

    @Override
    protected Map<String, String> getMasterFlags() {
        Map<String, String> flagMap = super.getMasterFlags();
        toggleDDLMode(flagMap, /* useLegacy */ false);
        return flagMap;
    }

    @Override
    public int getTestMethodTimeoutSec() {
        return 1800;
    }

    @Test
    public void testPgRegressPasswordHistory() throws Exception {
        runPgRegressTest("yb_password_history_schedule");
    }

    /**
     * MD5 of a password is deterministic, so setting the password that is already stored leaves
     * pg_authid unchanged. The reuse check still rejects it when the role changes its own
     * password, and that error is raised before a history row is written. An admin reset
     * records without that check, so this ALTER runs as the role.
     */
    @Test
    public void testSameMd5PasswordSkipsHistory() throws Exception {
        final String role = "phsame";

        try (Connection setup = getConnectionBuilder().connect();
             Statement admin = setup.createStatement()) {
            admin.execute("SET password_encryption = 'md5'");
            admin.execute("CREATE USER " + role + " PASSWORD 'pw_same'");
            assertEquals(1, historyLength(admin, role));

            try (Connection own = getConnectionBuilder().withUser(role).connect();
                 Statement s = own.createStatement()) {
                s.execute("SET password_encryption = 'md5'");
                try {
                    s.execute("ALTER USER " + role + " PASSWORD 'pw_same'");
                    fail("same MD5 password was accepted");
                } catch (PSQLException e) {
                    assertTrue(e.getMessage().contains(
                        "password matches a recently used password"));
                }
            }

            assertEquals(1, historyLength(admin, role));
        }
    }

    private static int historyLength(Statement s, String role) throws SQLException {
        ResultSet rs = s.executeQuery(
            "SELECT count(*) FROM pg_catalog.pg_yb_password_history h " +
            "JOIN pg_authid a ON a.oid = h.pwdhstrole WHERE a.rolname = '" + role + "'");
        assertTrue(rs.next());
        return rs.getInt(1);
    }
}

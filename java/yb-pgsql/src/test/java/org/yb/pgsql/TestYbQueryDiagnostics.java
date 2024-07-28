// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.ResultSet;
import java.sql.Statement;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import org.yb.YBTestRunner;

import java.util.Collections;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(value = YBTestRunner.class)
public class TestYbQueryDiagnostics extends BasePgSQLTest {

    private void setQueryDiagnosticsConfigAndRestartCluster() throws Exception {
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("TEST_yb_enable_query_diagnostics","true");
        restartClusterWithFlags(Collections.emptyMap(), flagMap);
    }

    @Test
    public void checkBindVariablesData() throws Exception {
        setQueryDiagnosticsConfigAndRestartCluster();
        String queryId = null;
        Path bundleDataPath = null, bindVarPath = null;

        try (Statement statement = connection.createStatement()) {

            statement.execute("CREATE TABLE test_table(a TEXT, b INT, c FLOAT)");
            statement.execute("PREPARE stmt(TEXT, INT, FLOAT) AS SELECT * FROM test_table " +
                              "WHERE a = $1 AND b = $2 AND c = $3;");
            statement.execute("EXECUTE stmt('test', 1, 1.1)");

            try (ResultSet resultSet = statement.executeQuery("SELECT queryid FROM " +
            "pg_stat_statements WHERE query LIKE 'PREPARE%'")) {
                if (resultSet.next()) {
                    queryId = resultSet.getString("queryid");
                }
            }

            String query = "SELECT * FROM yb_query_diagnostics(" + queryId +
                            ",diagnostics_interval_sec => 10" +
                            ",bind_var_query_min_duration_ms => 0)";

            ResultSet rs = statement.executeQuery(query);
            if (rs.next()) {
                bundleDataPath = Paths.get(rs.getString("yb_query_diagnostics"));
            }
            statement.execute("EXECUTE stmt('test1', 2, 2.2)");
            statement.execute("EXECUTE stmt('test2', 3, 3.3)");
            Thread.sleep(11000);

            bindVarPath = bundleDataPath.resolve("bind_variables.csv");

            assertTrue("Bind variables file does not exist", Files.exists(bindVarPath));
            assertGreaterThan("bind_variables.csv file size is not greater than 0",
                               Files.size(bindVarPath) , 0L);
        }
    }
}

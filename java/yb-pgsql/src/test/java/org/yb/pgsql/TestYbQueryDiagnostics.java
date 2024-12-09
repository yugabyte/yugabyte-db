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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertLessThan;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.Timestamp;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Scanner;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

import org.json.JSONObject;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestYbQueryDiagnostics extends BasePgSQLTest {
    public class QueryDiagnosticsParams {
        private final int diagnosticsInterval;
        private final int explainSampleRate;
        private final boolean explainAnalyze;
        private final boolean explainDist;
        private final boolean explainDebug;
        private final int bindVarQueryMinDuration;

        public QueryDiagnosticsParams(int diagnosticsInterval, int explainSampleRate,
                                      boolean explainAnalyze, boolean explainDist,
                                      boolean explainDebug, int bindVarQueryMinDuration) {
            this.diagnosticsInterval = diagnosticsInterval;
            this.explainSampleRate = explainSampleRate;
            this.explainAnalyze = explainAnalyze;
            this.explainDist = explainDist;
            this.explainDebug = explainDebug;
            this.bindVarQueryMinDuration = bindVarQueryMinDuration;
        }
    }

    public class QueryDiagnosticsStatus {
        private final Path path;
        private final String status;
        private final String description;
        private final QueryDiagnosticsParams params;

        public QueryDiagnosticsStatus(Path path, String status, String description,
                                      QueryDiagnosticsParams params) {
            this.path = path;
            this.status = status;
            this.description = description;
            this.params = params;
        }
    }

    private static final Logger LOG = LoggerFactory.getLogger(TestYbQueryDiagnostics.class);
    private static final AtomicInteger queryIdGenerator = new AtomicInteger();
    private static final int ASH_SAMPLING_INTERVAL_MS = 500;
    private static final int YB_QD_MAX_EXPLAIN_PLAN_LEN = 16384;
    private static final int YB_QD_MAX_BIND_VARS_LEN = 2048;
    private static final int BG_WORKER_INTERVAL_MS = 1000;
    private static final String noQueriesExecutedWarning = "No query executed;";
    private static final String pgssResetWarning =
        "pg_stat_statements was reset, query string not available;";
    private static final String permissionDeniedWarning =
        "Failed to create query diagnostics directory, Permission denied;";
    private static final String preparedStmt =
        "PREPARE stmt(text, int, float) AS " +
        "SELECT * FROM test_table1 WHERE a = $1 AND b = $2 AND c = $3";

    @Before
    public void setUp() throws Exception {
        /* Set Gflags and restart cluster */
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("TEST_yb_enable_query_diagnostics", "true");

        /* Required for some of the fields within schema details */
        flagMap.put("ysql_beta_features", "true");

        restartClusterWithFlags(Collections.emptyMap(), flagMap);

        setUpPreparedStatement();
    }

    public void setUp(int queryDiagnosticsCircularBufferSize) throws Exception {
        /* Set Gflags and restart cluster */
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("TEST_yb_enable_query_diagnostics", "true");
        appendToYsqlPgConf(flagMap,
                            "yb_query_diagnostics_circular_buffer_size=" +
                            queryDiagnosticsCircularBufferSize);

        /* Required for some of the fields within schema details */
        flagMap.put("ysql_beta_features", "true");

        restartClusterWithFlags(Collections.emptyMap(), flagMap);

        setUpPreparedStatement();
    }

    /*
     * Sets up a prepared statement for testing.
     */
    public void setUpPreparedStatement() throws Exception {
        try (Statement statement = connection.createStatement()) {
            /* Creating test table and filling dummy data */
            statement.execute("CREATE TABLE test_table1(a TEXT, b INT, c FLOAT)");
            statement.execute(preparedStmt);
            statement.execute("EXECUTE stmt('var', 1, 1.1)");
        }
    }

    /*
     * Retrieves the query ID from pg_stat_statements based on the provided pattern.
     */
    private String getQueryIdFromPgStatStatements(Statement statement, String pattern)
                                                    throws Exception {
        /* Get query id of the prepared statement */
        ResultSet resultSet = statement.executeQuery("SELECT queryid FROM pg_stat_statements " +
                                                        "WHERE query LIKE '" + pattern + "'");
        if (!resultSet.next())
            fail("Query id not found in pg_stat_statements");

        return resultSet.getString("queryid");
    }

    private String extractQueryTextFromPgssFile(Statement statement, Path pgssPath)
        throws Exception {
        String queryText = null;

        try (Scanner scanner = new Scanner(pgssPath)) {
            // Skip header line
            scanner.nextLine();

            // Use comma as delimiter but keep quoted content together
            scanner.useDelimiter(",(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)");

            // Skip queryid (first column)
            scanner.next();

            // Get query text (second column) and remove surrounding quotes
            queryText = scanner.next().replaceAll("^\"|\"$", "");
        }

        return queryText;
    }
    /*
     * Generates a unique query ID.
     *
     * @return A unique query ID as a string.
     */
    private String generateUniqueQueryId() {
        return String.valueOf(queryIdGenerator.incrementAndGet());
    }

    /*
     * Returns the last bundle data from the view
     * which corresponds to queryId and satisfies the whereClause.
     */
    private QueryDiagnosticsStatus getViewData(Statement statement, String queryId,
                                                String whereClause) throws Exception {
        StringBuilder query = new StringBuilder()
                    .append("SELECT * FROM yb_query_diagnostics_status WHERE query_id = '")
                    .append(queryId).append("'");

        if (!whereClause.isEmpty()) {
            query.append(" AND (").append(whereClause).append(")");
        }

        query.append(" ORDER BY start_time DESC LIMIT 1");

        ResultSet resultSet = statement.executeQuery(query.toString());

        if (!resultSet.next())
            fail("yb_query_diagnostics_status view does not have expected data");

        String explainParamsString = resultSet.getString("explain_params");
        JSONObject explainParams = new JSONObject(explainParamsString);

        QueryDiagnosticsParams lastBundleParams = new QueryDiagnosticsParams(
            resultSet.getInt("diagnostics_interval_sec"),
            explainParams.getInt("explain_sample_rate"),
            explainParams.getBoolean("explain_analyze"),
            explainParams.getBoolean("explain_dist"),
            explainParams.getBoolean("explain_debug"),
            resultSet.getInt("bind_var_query_min_duration_ms"));


        return new QueryDiagnosticsStatus(Paths.get(resultSet.getString("path")),
                                            resultSet.getString("status"),
                                            resultSet.getString("description"),
                                            lastBundleParams);
    }

    /*
     * Asserts that the expected and actual query diagnostics status objects are equal.
     */
    private void assertQueryDiagnosticsStatus(QueryDiagnosticsStatus expected,
                                              QueryDiagnosticsStatus actual) throws SQLException {
        assertEquals("yb_query_diagnostics_status returns wrong path",
                        expected.path, actual.path);
        assertEquals("yb_query_diagnostics_status returns wrong status",
                        expected.status, actual.status);
        assertTrue("yb_query_diagnostics_status description does not contain expected description",
                    actual.description.contains(expected.description));
        assertEquals("yb_query_diagnostics_status returns wrong diagnostics_interval_sec",
                    expected.params.diagnosticsInterval, actual.params.diagnosticsInterval);
        assertEquals("yb_query_diagnostics_status returns wrong" +
                        "bind_var_query_min_duration_ms",
                        expected.params.bindVarQueryMinDuration,
                        actual.params.bindVarQueryMinDuration);
        assertEquals("yb_query_diagnostics_status returns wrong explain_params",
                        expected.params.explainAnalyze, actual.params.explainAnalyze);
        assertEquals("yb_query_diagnostics_status returns wrong explain_params",
                        expected.params.explainDebug, actual.params.explainDebug);
        assertEquals("yb_query_diagnostics_status returns wrong explain_params",
                        expected.params.explainDist, actual.params.explainDist);
        assertEquals("yb_query_diagnostics_status returns wrong explain_params",
                        expected.params.explainSampleRate, actual.params.explainSampleRate);
    }

    /*
     * Prints the results of a SQL query in a formatted table.
     *
     * The output includes:
     * - Query string
     * - Column headers
     * - Row data with aligned columns
     * - Separator lines for readability
     *
     * NULL values are displayed as "NULL" in the output.
     */
    private void printQueryResults(Statement statement, String query) throws SQLException {
        LOG.info("Printing query results for: " + query);
        try (ResultSet rs = statement.executeQuery(query)) {
            // Get result set metadata
            ResultSetMetaData metadata = rs.getMetaData();
            int columnCount = metadata.getColumnCount();

            // Print column headers
            StringBuilder header = new StringBuilder();
            for (int i = 1; i <= columnCount; i++) {
                if (i > 1) {
                    header.append(" | ");
                }
                header.append(String.format("%-20s", metadata.getColumnName(i)));
            }

            String separator = createSeparator(header.length());
            LOG.info("\nQuery: " + query);
            LOG.info(separator);
            LOG.info(header.toString());
            LOG.info(separator);

            // Print rows
            while (rs.next()) {
                StringBuilder row = new StringBuilder();
                for (int i = 1; i <= columnCount; i++) {
                    if (i > 1) {
                        row.append(" | ");
                    }
                    String value = rs.getString(i);
                    row.append(String.format("%-20s", value != null ? value : "NULL"));
                }
                LOG.info(row.toString());
            }
            LOG.info(separator + "\n");
        }
    }

    /*
     * Creates a separator line of specified length using dashes.
     */
    private String createSeparator(int length) {
        StringBuilder separator = new StringBuilder();
        for (int i = 0; i < length; i++) {
            separator.append("-");
        }
        return separator.toString();
    }

    /*
     * Runs query diagnostics on the given query ID and parameters.
     */
    private Path runQueryDiagnostics(Statement statement, String queryId,
                                     QueryDiagnosticsParams params) throws Exception {
        /* Run query diagnostics on the prepared stmt */
        String query = "SELECT * FROM yb_query_diagnostics( " +
                       "query_id => " + queryId +
                       ",diagnostics_interval_sec => " + params.diagnosticsInterval +
                       ",bind_var_query_min_duration_ms =>" + params.bindVarQueryMinDuration +
                       ",explain_analyze => " + params.explainAnalyze +
                       ",explain_dist => " + params.explainDist +
                       ",explain_debug => " + params.explainDebug +
                       ",explain_sample_rate => " + params.explainSampleRate + ")";
        ResultSet resultSet = statement.executeQuery(query);

        if (!resultSet.next())
            fail("yb_query_diagnostics() function failed");

        /* Returns bundle path */
        return Paths.get(resultSet.getString("yb_query_diagnostics"));
    }

    /*
     * Waits for the bundle to complete by checking the yb_query_diagnostics_status view.
     */
    private void waitForBundleCompletion(String queryId, Statement statement,
                                         int diagnosticsInterval) throws Exception {
        Thread.sleep(diagnosticsInterval * 1000 + BG_WORKER_INTERVAL_MS);

        long start_time = System.currentTimeMillis();
        while (true)
        {
            System.out.println("Waiting in forever loop");

            ResultSet resultSet = statement.executeQuery(
                                  "SELECT * FROM yb_query_diagnostics_status where query_id = " +
                                  queryId);

            if (resultSet.next() && !resultSet.getString("status").equals("In Progress"))
                break;

            if (System.currentTimeMillis() - start_time > 60000) // 1 minute
                fail("Bundle did not complete within the expected time");

            Thread.sleep(BG_WORKER_INTERVAL_MS);
        }
    }

    /*
     * Counts the number of table sections in the schema details.
     */
    private int countTableSections(String schemaDetails) {
        Pattern tablePattern = Pattern.compile("Table name: ");
        Matcher matcher = tablePattern.matcher(schemaDetails);
        int count = 0;
        while (matcher.find()) {
            count++;
        }
        return count;
    }

    /*
     * Recreates a folder with specified permissions.
     */
    private void recreateFolderWithPermissions(String query_diagnostics_path, int permissions)
    throws Exception {
        List<String> commands = Arrays.asList(
            "rm -rf " + query_diagnostics_path,
            "mkdir " + query_diagnostics_path,
            "chmod " + permissions + " " + query_diagnostics_path
        );
        execute(commands);
    }

    /*
     * Executes a list of commands in a shell.
     */
    private static void execute(List<String> commands) throws Exception{
        for (String command : commands) {
            Process process = Runtime.getRuntime().exec(new String[]{"bash", "-c", command});
            process.waitFor();
        }
    }

    /*
     * Runs multiple bundles and checks for circular buffer wrap-around.
     */
    private void runMultipleBundles(Statement statement, int n) throws Exception {
        int diagnosticsInterval = 1;
        int queryId = 0;

        QueryDiagnosticsParams params = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            false /* explainAnalyze */,
            false/* explainDist */,
            false/* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        for (int i = 1; i <= n; i+=5) {

            /* Run in batches of 5 to save time */
            for (int j = 0; j < 5; j++)
            {
                queryId++;
                runQueryDiagnostics(statement, Integer.toString(queryId) , params);
            }

            /* Completing the bundles as circular buffer only stores completed bundles */
            Thread.sleep((diagnosticsInterval + 1) * 1000);

            /* If the buffer wrapped around then query_id 1 should not be present */
            ResultSet resultSet = statement.executeQuery(String.format("SELECT query_id " +
                                                 "FROM yb_query_diagnostics_status " +
                                                 "where query_id = '1'"));
            if (!resultSet.next()) {
                /* Check that the last 5 query_ids are present in the view */
                for (int k = queryId; k > queryId - 5; k--) {
                    resultSet = statement.executeQuery(String.format("SELECT query_id " +
                                                       "FROM yb_query_diagnostics_status " +
                                                       "WHERE query_id = '%d'", k));
                    if (!resultSet.next())
                        fail("could not find query_id " + k + " in the view");
                }
                return;
            }
        }
        fail("Buffer never wrapped around");
    }

    /*
     * Counts the number of occurrences of a regex pattern in a string.
     */
    private int countOccurrences(String content, String regex) {
        Pattern pattern = Pattern.compile(regex);
        Matcher matcher = pattern.matcher(content);
        int count = 0;
        while (matcher.find()) {
            count++;
        }
        return count;
    }

    /*
     * Runs a bundle with the given queries and checks for the specified warning.
     */
    private void runBundleWithQueries(Statement statement, String queryId,
                                      QueryDiagnosticsParams queryDiagnosticsParams,
                                      String[] queries, String warning) throws Exception {
        Path bundleDataPath = runQueryDiagnostics(statement, queryId, queryDiagnosticsParams);

        for (String query : queries) {
            statement.execute(query);
        }

        waitForBundleCompletion(queryId, statement, queryDiagnosticsParams.diagnosticsInterval);

        /* Select the last executed bundle */
        QueryDiagnosticsStatus bundleViewEntry = getViewData(statement, queryId, "");

        QueryDiagnosticsStatus expectedBundleViewEntry = new QueryDiagnosticsStatus(
            bundleDataPath, "Success", warning, queryDiagnosticsParams);

        assertQueryDiagnosticsStatus(expectedBundleViewEntry, bundleViewEntry);

        if (!warning.contains(noQueriesExecutedWarning)) {
            Path pgssPath = bundleDataPath.resolve("pg_stat_statements.csv");
            assertTrue("pg_stat_statements file does not exist", Files.exists(pgssPath));
            assertGreaterThan("pg_stat_statements.csv file is empty",
            Files.size(pgssPath) , 0L);

            /* Read the pg_stat_statements.csv file */
            List<String> pgssData = Files.readAllLines(pgssPath);
            String[] tokens = pgssData.get(1).split(",");

            /* Ensure that the query string in pg_stat_statements is empty as expected */
            assertEquals("pg_stat_statements query is incorrect", "\"\"", tokens[1]);
        }
    }

    /*
     * Creates tables for testing schema details.
     */
    private void createTestingTables(Statement statement) throws Exception {
        /* Create a table with a primary key, unique key, foreign key, and a partition key */
        statement.execute("CREATE TABLE test_table (\n" + //
                            "  id INTEGER PRIMARY KEY,\n" + //
                            "  name VARCHAR(100) NOT NULL,\n" + //
                            "  description TEXT,\n" + //
                            "  updated_at TIMESTAMP WITH TIME ZONE DEFAULT " + //
                            "CURRENT_TIMESTAMP,\n" + //
                            "  is_active BOOLEAN DEFAULT true,\n" + //
                            "  price NUMERIC(10, 2),\n" + //
                            "  UNIQUE (id, description)\n" + //
                            ")\n" + //
                            "PARTITION BY RANGE (id);");
        /* Add a "Check constraint" in test_table */
        statement.execute("ALTER TABLE test_table " + //
                            "ADD CONSTRAINT check_price_positive CHECK (price >= 0);");
        /* Create an index on test_table */
        statement.execute("CREATE INDEX idx_test_table_name ON test_table (name);");
        /* Create a rule on test_table */
        statement.execute("CREATE RULE test_rule AS ON DELETE TO test_table\n" + //
                            "DO INSTEAD UPDATE test_table SET is_active = false " + //
                            "WHERE id = OLD.id;");
        /* Add policy in test_table */
        statement.execute("ALTER TABLE test_table ENABLE ROW LEVEL SECURITY;\n" + //
                            "CREATE POLICY test_policy ON test_table " + //
                            "FOR SELECT USING (is_active = true);");

        /* Create a sequence */
        statement.execute("CREATE SEQUENCE test_sequence\n" + //
                            "  INCREMENT 1\n" + //
                            "  START 1000\n" + //
                            "  MINVALUE 1000\n" + //
                            "  MAXVALUE 9999;");
        statement.execute("ALTER SEQUENCE test_sequence OWNED BY test_table.id;");

        statement.execute("CREATE TABLE table_referenced_by_view (" + //
                            "  id INTEGER PRIMARY KEY," + //
                            "  name VARCHAR(100));");
        /* Create a view */
        statement.execute("CREATE VIEW test_view AS \n" + //
                            "  SELECT id, name \n" + //
                            "  FROM table_referenced_by_view\n" + //
                            "  WHERE id >= 0;");

        /* Create a partitioned table over test_table */
        statement.execute("CREATE TABLE test_table_partition PARTITION OF test_table\n" + //
                            "  FOR VALUES FROM (0) TO (5);");
        /* Create a trigger on test_table_partition */
        statement.execute("CREATE FUNCTION update_timestamp()\n" + //
                            "RETURNS TRIGGER AS $$\n" + //
                            "BEGIN\n" + //
                            "  NEW.updated_at = CURRENT_TIMESTAMP;\n" + //
                            "  RETURN NEW;\n" + //
                            "END;\n" + //
                            "$$ LANGUAGE plpgsql;");
        statement.execute("CREATE TRIGGER trigger_update_timestamp\n" + //
                            "  BEFORE UPDATE ON test_table_partition\n" + //
                            "  FOR EACH ROW\n" + //
                            "  EXECUTE FUNCTION update_timestamp();");
        /* Create a foreign key on test_table_partition */
        statement.execute("CREATE TABLE referencing_table (\n" + //
                            "  id INTEGER PRIMARY KEY,\n" + //
                            "  test_table_id INTEGER REFERENCES test_table_partition(id),\n" + //
                            "  related_data TEXT UNIQUE);");
        /* Create a publication on test_table_partition */
        statement.execute("CREATE PUBLICATION test_publication FOR TABLE test_table_partition;");

        /*
            * Since foreign data wrappers are not switched on by default,
            * and are part of ysql_beta_options we create the extension manually.
            */
        statement.execute("CREATE EXTENSION IF NOT EXISTS postgres_fdw;");
        statement.execute("CREATE SERVER foreign_server\n" + //
                            "FOREIGN DATA WRAPPER postgres_fdw\n" + //
                            "OPTIONS (host 'remote_host', dbname 'remote_database', port '5432');");
        statement.execute("CREATE USER MAPPING FOR CURRENT_USER\n" + //
                            "SERVER foreign_server\n" + //
                            "OPTIONS (user 'remote_user', password 'remote_password');");
        /* Create a foreign table */
        statement.execute("CREATE FOREIGN TABLE foreign_test_table (\n" + //
                            "  id INTEGER,\n" + //
                            "  name VARCHAR(100))\n" + //
                            "SERVER foreign_server\n" + //
                            "OPTIONS (schema_name 'public', table_name 'remote_test_table');");
    }

    /*
     * Tests the dumping of bind varaibles data in the case of a prepared statement.
     */
    @Test
    public void checkBindVariablesData() throws Exception {
        int diagnosticsInterval = 2;
        QueryDiagnosticsParams params = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        try (Statement statement = connection.createStatement()) {
            /* Run query diagnostics on the prepared stmt */
            String queryId = getQueryIdFromPgStatStatements(statement, "PREPARE%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, params);

            /* Generate some data to be dumped */
            statement.execute("EXECUTE stmt('var1', 1, 1.1)");
            statement.execute("EXECUTE stmt('var2', 2, 2.2)");

            waitForBundleCompletion(queryId, statement, diagnosticsInterval);

            /* Check constants_and_bind_variables.csv file */
            Path bindVarPath = bundleDataPath.resolve("constants_and_bind_variables.csv");

            assertTrue("constants_and_bind_variables.csv does not exist",
                       Files.exists(bindVarPath));
            assertGreaterThan("constants_and_bind_variables.csv file size is not greater than 0",
                              Files.size(bindVarPath) , 0L);

            List<String> lines = Files.readAllLines(bindVarPath);
            assertEquals("Number of lines in constants_and_bind_variables.csv is not as expected",
                         lines.size(), 2);
            assertTrue("constants_and_bind_variables.csv does not contain expected data",
                       lines.get(0).contains("'var1',1,1.1") &&
                       lines.get(1).contains("'var2',2,2.2"));

            Path pgssPath = bundleDataPath.resolve("pg_stat_statements.csv");

            assertTrue("pg_stat_statements.csv does not exist", Files.exists(pgssPath));
            assertGreaterThan("pg_stat_statements.csv file is empty",
                                Files.size(pgssPath) , 0L);

            String queryText = extractQueryTextFromPgssFile(statement, pgssPath);
            if (queryText == null)
                fail("Query text not found in pg_stat_statements.csv");

            LOG.info("Pgss query text: " + queryText);
            assertTrue("pg_stat_statements query is incorrect",
                       queryText.contains(preparedStmt));
        }
    }

    /*
     * Tests the dumping of bind variables data in the case of a non-prepared query.
     */
    @Test
    public void checkConstantsData() throws Exception {
        int diagnosticsInterval = 2;
        QueryDiagnosticsParams params = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);
        String query = "INSERT INTO test_table1 VALUES ('abcd', 2, 3)";
        String expectedNormalizedQuery = "INSERT INTO test_table1 VALUES ($1, $2, $3)";
        try (Statement statement = connection.createStatement()) {
            statement.execute(query);

            /* Run query diagnostics on the normal query */
            String queryId = getQueryIdFromPgStatStatements(statement, "INSERT INTO test_table1%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, params);

            statement.execute(query);

            waitForBundleCompletion(queryId, statement, diagnosticsInterval);

            /* Check constants_and_bind_variables.csv file */
            Path bindVarPath = bundleDataPath.resolve("constants_and_bind_variables.csv");
            assertTrue("constants_and_bind_variables.csv does not exist",
                       Files.exists(bindVarPath));
            assertGreaterThan("constants_and_bind_variables.csv file size is not greater than 0",
                              Files.size(bindVarPath), 0L);

            List<String> bindVarLines = Files.readAllLines(bindVarPath);
            assertEquals("Number of lines in constants_and_bind_variables.csv is not as expected",
                         bindVarLines.size(), 1);
            assertTrue("constants_and_bind_variables.csv does not contain expected data",
                       bindVarLines.get(0).contains("'abcd',2,3"));

            Path pgssPath = bundleDataPath.resolve("pg_stat_statements.csv");

            assertTrue("pg_stat_statements.csv does not exist", Files.exists(pgssPath));
            assertGreaterThan("pg_stat_statements.csv file is empty",
                                Files.size(pgssPath) , 0L);

            String queryText = extractQueryTextFromPgssFile(statement, pgssPath);
            if (queryText == null)
                fail("Query text not found in pg_stat_statements.csv");

            LOG.info("Pgss query text: " + queryText);
            assertTrue("pg_stat_statements query is incorrect",
                       queryText.contains(expectedNormalizedQuery));
        }
    }

    /*
     * Tests the dumping of bind variables and constants in the case of a
     * prepared statement haveing hard coded values.
     */
    @Test
    public void testMixedConstantsAndBindVariables() throws Exception {
        int diagnosticsInterval = 2;
        QueryDiagnosticsParams params = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);
        String prepareQuery = "PREPARE stmt1(text, float) AS " +
                              "SELECT * FROM test_table1 WHERE a = $1 AND b = 99 AND c = $2";
        String executeQuery = "EXECUTE stmt1('abcd', 1.1)";
        String expectedNormalizedQuery = "PREPARE stmt1(text, float) AS " +
                              "SELECT * FROM test_table1 WHERE a = $1 AND b = $3 AND c = $2";
        try (Statement statement = connection.createStatement()) {
            statement.execute(prepareQuery);
            statement.execute(executeQuery);

            /* Run query diagnostics on the normal query */
            String queryId = getQueryIdFromPgStatStatements(statement,
                                                            "PREPARE stmt1(text, float)%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, params);

            statement.execute(executeQuery);

            waitForBundleCompletion(queryId, statement, diagnosticsInterval);

            /* Check constants_and_bind_variables.csv file */
            Path bindVarPath = bundleDataPath.resolve("constants_and_bind_variables.csv");
            assertTrue("constants_and_bind_variables.csv does not exist",
                       Files.exists(bindVarPath));
            assertGreaterThan("constants_and_bind_variables.csv file size is not greater than 0",
                              Files.size(bindVarPath), 0L);

            List<String> bindVarLines = Files.readAllLines(bindVarPath);
            assertEquals("Number of lines in constants_and_bind_variables.csv is not as expected",
                         bindVarLines.size(), 1);
            assertTrue("constants_and_bind_variables.csv does not contain expected data",
                       bindVarLines.get(0).contains("'abcd',1.1"));

            Path pgssPath = bundleDataPath.resolve("pg_stat_statements.csv");

            assertTrue("pg_stat_statements.csv does not exist", Files.exists(pgssPath));
            assertGreaterThan("pg_stat_statements.csv file is empty",
                                Files.size(pgssPath) , 0L);

            String queryText = extractQueryTextFromPgssFile(statement, pgssPath);
            if (queryText == null)
                fail("Query text not found in pg_stat_statements.csv");

            LOG.info("Pgss query text: " + queryText);
            assertTrue("pg_stat_statements query is incorrect",
                        queryText.contains(expectedNormalizedQuery));
        }
    }

    /*
     * Verifies the functionality of the yb_query_diagnostics_status view.
     *
     * This test creates three different query diagnostics bundles:
     * 1. A successful bundle that completes normally
     * 2. A bundle that encounters a file permission error
     * 3. A long-running bundle that remains in progress
     *
     * For the successful bundle:
     * - Verifies that the bundle completes with "Success" status
     * - Checks for "No query executed" warning when no queries are run
     *
     * For the error bundle:
     * - Creates a permission error by restricting directory access
     * - Verifies that the bundle has "Error" status
     * - Confirms the correct error message about permission denial
     *
     * For the in-progress bundle:
     * - Sets a long diagnostics interval (120 seconds)
     * - Verifies that the bundle shows "In Progress" status
     *
     * Each bundle's status, path, description, and parameters are verified
     * against expected values in the yb_query_diagnostics_status view.
     */
    @Test
    public void testYbQueryDiagnosticsStatus() throws Exception {
        try (Statement statement = connection.createStatement()) {
            /*
             * We use random number for the queryid, as we are not doing any accumulation of data,
             * the specific number for the query ID does not matter.
             */

            /*
             * Successful bundle
             */
            String successfulQueryId = generateUniqueQueryId();
            int diagnosticsInterval = 2;
            QueryDiagnosticsParams successfulRunParams = new QueryDiagnosticsParams(
                diagnosticsInterval,
                100 /* explainSampleRate */,
                true /* explainAnalyze */,
                true /* explainDist */,
                false /* explainDebug */,
                0 /* bindVarQueryMinDuration */);

            /* Trigger the successful bundle */
            Path successfulBundlePath = runQueryDiagnostics(statement, successfulQueryId,
                                                            successfulRunParams);

            /* Wait until yb_query_diagnostics_status entry's status becomes Success/Error */
            waitForBundleCompletion(successfulQueryId, statement, diagnosticsInterval);

            /* Assert that the Success bundle is present in the view */
            QueryDiagnosticsStatus successBundleViewEntry = getViewData(statement,
                                                                        successfulQueryId,
                                                                        "status='Success'");
            /* Create the expected bundle data */
            QueryDiagnosticsStatus expectedSuccessBundleViewEntry = new QueryDiagnosticsStatus(
                successfulBundlePath, "Success", noQueriesExecutedWarning, successfulRunParams);

            /* Assert that the bundle data is as expected */
            assertQueryDiagnosticsStatus(expectedSuccessBundleViewEntry, successBundleViewEntry);

            /*
             * Error bundle
             */
            String queryDiagnosticsPath = successfulBundlePath.getParent().getParent().toString();
            String errorQueryId = generateUniqueQueryId();
            QueryDiagnosticsParams fileErrorRunParams = new QueryDiagnosticsParams(
                diagnosticsInterval,
                50 /* explainSampleRate */,
                true /* explainAnalyze */,
                true /* explainDist */,
                true /* explainDebug */,
                10 /* bindVarQueryMinDuration */);

            /* Postgres cannot write to query-diagnostics folder */
            recreateFolderWithPermissions(queryDiagnosticsPath, 400);

            /* Trigger the bundle and raise file error */
            Path fileErrorBundlePath = runQueryDiagnostics(statement, errorQueryId,
                                                           fileErrorRunParams);

            /* Wait until yb_query_diagnostics_status entry's status becomes Error */
            waitForBundleCompletion(errorQueryId, statement, diagnosticsInterval);

            /* Print the query results for debugging */
            printQueryResults(statement, "SELECT * FROM yb_query_diagnostics_status");

            /* Assert that the Error bundle is present in the view */
            QueryDiagnosticsStatus errorBundleViewEntry = getViewData(statement, errorQueryId,
                                                                      "status='Error'");
            /* Create the expected bundle data */
            QueryDiagnosticsStatus expectedErrorBundleViewEntry = new QueryDiagnosticsStatus(
                fileErrorBundlePath, "Error", permissionDeniedWarning,
                fileErrorRunParams);

            /* Assert that the bundle data is as expected */
            assertQueryDiagnosticsStatus(expectedErrorBundleViewEntry, errorBundleViewEntry);

            /* Reset permissions to allow test cleanup */
            recreateFolderWithPermissions(queryDiagnosticsPath, 666);

            /*
             * In Progress bundle
             */
            String inProgressQueryId = generateUniqueQueryId();
            QueryDiagnosticsParams inProgressRunParams = new QueryDiagnosticsParams(
                120 /* diagnosticsInterval */,
                75 /* explainSampleRate */,
                false /* explainAnalyze */,
                false /* explainDist */,
                false /* explainDebug */,
                15 /* bindVarQueryMinDuration */);

            /* Trigger the bundle for 120 seconds to ensure it remains in In Progress state */
            Path inProgressBundlePath = runQueryDiagnostics(statement, inProgressQueryId,
                                                            inProgressRunParams);

            /* Assert that the In Progress bundle is present in the view */
            QueryDiagnosticsStatus inProgressBundleViewEntry = getViewData(statement,
                                                                      inProgressQueryId,
                                                                      "status='In Progress'");
            /* Create the expected bundle data */
            QueryDiagnosticsStatus expectedInProgressBundleViewEntry = new QueryDiagnosticsStatus(
                inProgressBundlePath, "In Progress", "", inProgressRunParams);
            assertQueryDiagnosticsStatus(expectedInProgressBundleViewEntry,
                                         inProgressBundleViewEntry);
        }
    }

    @Test
    public void testBufferSizeUpdateAfterShmemInit() throws Exception {
        try (Statement statement = connection.createStatement()) {
            try {
               statement.executeQuery("set yb_query_diagnostics_circular_buffer_size to 50");
            }
            catch (SQLException e) {
                assertTrue("Error message does not contain expected message",
                           e.getMessage().contains("parameter" +
                           " \"yb_query_diagnostics_circular_buffer_size\" " +
                           "cannot be changed without restarting the server"));
            }
        }
    }

    @Test
    public void testCircularBufferWrapAround() throws Exception {
        setUp(15);

        try (Statement statement = connection.createStatement()) {
            /* running several bundles ensure buffer wraps around */
            runMultipleBundles(statement, 100);
        }
    }

    @Test
    public void checkAshData() throws Exception {
        int diagnosticsInterval = (5 * ASH_SAMPLING_INTERVAL_MS) / 1000; /* convert to seconds */
        QueryDiagnosticsParams params = new QueryDiagnosticsParams(
                diagnosticsInterval /* diagnosticsInterval */,
                100 /* explainSampleRate */,
                true /* explainAnalyze */,
                true /* explainDist */,
                false /* explainDebug */,
                0 /* bindVarQueryMinDuration */);

        /* sleep time is diagnosticsInterval + 1 sec to ensure that the bundle has expired */
        long sleep_time_s = diagnosticsInterval + 1;

        try (Statement statement = connection.createStatement()) {
            statement.execute("SELECT pg_sleep(0.5)");

            String queryId = getQueryIdFromPgStatStatements(statement, "%pg_sleep%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, params);

            /* Protects from "No query executed;" warning */
            statement.execute("SELECT pg_sleep(0.1)");

            /* sleep for bundle expiry */
            statement.execute("SELECT pg_sleep(" + sleep_time_s + ")");
            Path ashPath = bundleDataPath.resolve("active_session_history.csv");

            assertTrue("active_session_history file does not exist",
                       Files.exists(ashPath));
            assertGreaterThan("active_session_history.csv file is empty",
                              Files.size(ashPath) , 0L);

            ResultSet resultSet = statement.executeQuery(
                                  "SELECT * FROM yb_query_diagnostics_status");
            if (!resultSet.next())
                fail("yb_query_diagnostics_status view does not have expected data");

            Timestamp startTime = resultSet.getTimestamp("start_time");
            long diagnosticsIntervalSec = resultSet.getLong("diagnostics_interval_sec");
            Timestamp endTime = new Timestamp(startTime.getTime() +
                                              (diagnosticsIntervalSec * 1000L));

            statement.execute("CREATE TABLE temp_ash_data" +
                              "(LIKE yb_active_session_history INCLUDING ALL)");
            String copyCmd = "COPY temp_ash_data FROM '" + ashPath.toString() +
                             "' WITH (FORMAT CSV, HEADER)";
            statement.execute(copyCmd);

            resultSet = statement.executeQuery("SELECT COUNT(*) FROM temp_ash_data");
            long validTimeEntriesCount = getSingleRow(statement,
                                         "SELECT COUNT(*) FROM temp_ash_data").getLong(0);

            assertGreaterThan("active_session_history.csv file is empty",
                              validTimeEntriesCount, 0L);

            long invalidTimeEntriesCount = getSingleRow(statement,
                                           "SELECT COUNT(*) FROM temp_ash_data " +
                                           "WHERE sample_time < '" + startTime + "' " +
                                           "OR sample_time > '" + endTime + "'").getLong(0);

            assertEquals("active_session_history.csv contains invalid time entries",
                         invalidTimeEntriesCount, 0);
        }
    }

    @Test
    public void checkPgssData() throws Exception {
        int diagnosticsInterval = (5 * ASH_SAMPLING_INTERVAL_MS) / 1000; /* convert to seconds */
        QueryDiagnosticsParams queryDiagnosticsParams = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        /* sleep time is diagnosticsInterval + 1 sec to ensure that the bundle has expired */
        long sleep_time_s = diagnosticsInterval + 1;

        try (Statement statement = connection.createStatement()) {
            statement.execute("SELECT pg_sleep(0.5)");

            String queryId = getQueryIdFromPgStatStatements(statement, "%pg_sleep%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, queryDiagnosticsParams);

            statement.execute("SELECT pg_sleep(0.1)");
            statement.execute("SELECT * from pg_class");
            statement.execute("SELECT pg_sleep(0.2)");

            /* sleep for bundle expiry */
            statement.execute("SELECT pg_sleep(" + sleep_time_s + ")");

            Path pgssPath = bundleDataPath.resolve("pg_stat_statements.csv");

            assertTrue("pg_stat_statements file does not exist", Files.exists(pgssPath));
            assertGreaterThan("pg_stat_statements.csv file is empty",
                              Files.size(pgssPath) , 0L);

            /* Unit is ms */
            float epsilon = 10;
            int expectedTotalTime = 300;
            int expectedMinTime = 100;
            int expectedMaxTime = 200;
            int expectedMeanTime = 150;
            List<String> pgssData = Files.readAllLines(pgssPath);
            String[] tokens = pgssData.get(1).split(",");

            assertEquals("pg_stat_statements data size is not as expected",
                         2, pgssData.size());
            assertEquals("pg_stat_statements query is incorrect", queryId, tokens[0]);
            assertTrue("pg_stat_statements contains unnecessary data",
                       !tokens[1].contains("pg_class"));
            assertEquals("Number of calls are incorrect", "2", tokens[2]);
            /* pg_stat_statements outputs data in ms */
            assertLessThan("total_time is incorrect",
                           Math.abs(Float.parseFloat(tokens[3]) - expectedTotalTime), epsilon);
            assertLessThan("min_time is incorrect",
                           Math.abs(Float.parseFloat(tokens[4]) - expectedMinTime), epsilon);
            assertLessThan("max_time is incorrect",
                           Math.abs(Float.parseFloat(tokens[5]) - expectedMaxTime), epsilon);
            assertLessThan("mean_time is incorrect",
                           Math.abs(Float.parseFloat(tokens[6]) - expectedMeanTime), epsilon);
        }
    }

    @Test
    public void testPgssResetBetweenDiagnostics() throws Exception {
        int diagnosticsInterval = (5 * ASH_SAMPLING_INTERVAL_MS) / 1000; /* convert to seconds */
        QueryDiagnosticsParams queryDiagnosticsParams = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        try (Statement statement = connection.createStatement()) {
            /*
             * If pg_stat_statements resets during the bundle creation process,
             * the query string in the pg_stat_statements output file will not be available.
             * A warning will be included in the description field of the catalog view
             * to indicate the same.
             */
            String queryId = getQueryIdFromPgStatStatements(statement, "PREPARE%");

            /* Test different scenarios of pgss reset */

            /* reset */
            runBundleWithQueries(statement, queryId, queryDiagnosticsParams, new String[] {
                "SELECT pg_stat_statements_reset()",
            }, noQueriesExecutedWarning);

            /* statement -> reset */
            runBundleWithQueries(statement, queryId, queryDiagnosticsParams, new String[] {
                "EXECUTE stmt('var1', 1, 1.1)",
                "SELECT pg_stat_statements_reset()",
            }, pgssResetWarning);

            /* reset -> statement */
            runBundleWithQueries(statement, queryId, queryDiagnosticsParams, new String[] {
                "SELECT pg_stat_statements_reset()",
                "EXECUTE stmt('var2', 2, 2.2)"
            }, pgssResetWarning);

            /*
             * statement -> reset -> statement
             *
             * Note that this also emits pgssResetWarning, although a statement is executed
             * after the reset. This is intentional as if we were to implement a check for
             * last_time_query_bundled against last_time_reset, it would require a
             * GetCurrentTimestamp() call per bundled query, which could be expensive.
             */
            runBundleWithQueries(statement, queryId, queryDiagnosticsParams, new String[] {
                "EXECUTE stmt('var1', 1, 1.1)",
                "SELECT pg_stat_statements_reset()",
                "EXECUTE stmt('var2', 2, 2.2)"
            }, pgssResetWarning);
        }
    }

    @Test
    public void emptyBundle() throws Exception {
        int diagnosticsInterval = (5 * ASH_SAMPLING_INTERVAL_MS) / 1000; /* convert to seconds */
        QueryDiagnosticsParams params = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        /* sleep time is diagnosticsInterval + 1 sec to ensure that the bundle has expired */
        long sleep_time_s = diagnosticsInterval + 1;

        try (Statement statement = connection.createStatement()) {
            /* Run query diagnostics on the prepared stmt */
            String queryId = getQueryIdFromPgStatStatements(statement, "PREPARE%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, params);

            /* sleep for bundle expiry */
            statement.execute("SELECT pg_sleep(" + sleep_time_s + ")");

            /* Check that the bundle is empty */
            try (Stream<Path> files = Files.list(bundleDataPath)) {
                if (files.findAny().isPresent()) {
                    fail("The bundle directory is not empty, even though no queries were fired");
                }
            } catch (IOException e) {
                fail("Failed to list files in the bundle directory: " + e.getMessage());
            }

            /* Check that the bundle is empty in the view */
            QueryDiagnosticsStatus bundleViewEntry = getViewData(statement, queryId, "");

            QueryDiagnosticsStatus expectedBundleViewEntry = new QueryDiagnosticsStatus(
                bundleDataPath, "Success", noQueriesExecutedWarning, params);

            assertQueryDiagnosticsStatus(expectedBundleViewEntry, bundleViewEntry);
        }
    }

    /*
     * In this test we create a large variable and run the same query multiple times,
     * with the intent to overflow bind_variables and explain_plan buffers.
     */
    @Test
    public void testIntermediateFlushing() throws Exception {
        try (Statement statement = connection.createStatement()) {
            /* Run query diagnostics on the prepared stmt */
            String queryId = getQueryIdFromPgStatStatements(statement, "PREPARE%");

            /*
             * Ensures that query plans are not cached,
             * thereby unaffecting the explain plan output.
             */
            statement.execute("SET plan_cache_mode = 'force_custom_plan'");

            int minLen = Math.min(YB_QD_MAX_EXPLAIN_PLAN_LEN, YB_QD_MAX_BIND_VARS_LEN);
            int maxLen = Math.max(YB_QD_MAX_EXPLAIN_PLAN_LEN, YB_QD_MAX_BIND_VARS_LEN);

            /*
             * Since the flushing happens whenever the buffer is half full or more,
             * we consider only half the length.
             */
            int varLen = minLen / 2;
            String largeVariable = new String(new char[ varLen ]).replace('\0', 'a');

            /* To ensure that the buffer overflows we do twice as many iterations */
            int noOfIterations = (maxLen / varLen) + 1;

            int diagnosticsInterval = noOfIterations * (BG_WORKER_INTERVAL_MS / 1000);
            QueryDiagnosticsParams params = new QueryDiagnosticsParams(
                diagnosticsInterval,
                100 /* explainSampleRate */,
                true /* explainAnalyze */,
                true /* explainDist */,
                false /* explainDebug */,
                0 /* bindVarQueryMinDuration */);

            Path bundleDataPath = runQueryDiagnostics(statement, queryId, params);

            for (int i = 0; i < noOfIterations; i++) {
                statement.execute("execute stmt('" + largeVariable + "', 1, 1)");

                /* Sleep for a small duration to ensure that that we flush before the next query */
                Thread.sleep(BG_WORKER_INTERVAL_MS);
            }

            long start_time = System.currentTimeMillis();
            while (true)
            {
                ResultSet resultSet = statement.executeQuery(
                                      "SELECT * FROM yb_query_diagnostics_status");
                if (resultSet.next() && resultSet.getString("status").equals("Success"))
                    break;

                if (System.currentTimeMillis() - start_time > 60000) // 1 minute
                    fail("Bundle did not complete within the expected time");

                Thread.sleep(BG_WORKER_INTERVAL_MS);
            }

            /* Bundle has expired */
            Path bindVariablesPath = bundleDataPath.resolve("constants_and_bind_variables.csv");
            assertTrue("Bind variables file does not exist", Files.exists(bindVariablesPath));

            Path explainPlanPath = bundleDataPath.resolve("explain_plan.txt");
            assertTrue("Explain plan file does not exist", Files.exists(explainPlanPath));

            String bindVariablesContent = new String(Files.readAllBytes(bindVariablesPath));
            String explainPlanContent = new String(Files.readAllBytes(explainPlanPath));

            int bindVariablesLength = bindVariablesContent.length();
            int explainPlanLength = explainPlanContent.length();

            /* Verify that flushing did happen */
            assertTrue("Bind variables file was not flushed properly",
                       bindVariablesLength > YB_QD_MAX_BIND_VARS_LEN);
            assertTrue("Explain plan file was not flushed properly",
                       explainPlanLength > YB_QD_MAX_EXPLAIN_PLAN_LEN);

            /* Use regex to count occurrences of largeVariable in both files */
            String regex = Pattern.quote(largeVariable);
            int bindVariableCount = countOccurrences(bindVariablesContent, regex);
            int explainPlanCount = countOccurrences(explainPlanContent, regex);

            assertEquals("Variable should appear exactly " + noOfIterations +
                         " times in bind variables file", noOfIterations, bindVariableCount);
            assertEquals("Variable should appear exactly " + noOfIterations +
                         " times in explain plan file", noOfIterations, explainPlanCount);
        }
    }

    @Test
    public void checkSchemaDetailsData() throws Exception {
        int diagnosticsInterval = 2;
        QueryDiagnosticsParams queryDiagnosticsParams = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);
        String queryToBundle = "SELECT * FROM test_table \n" + //
                               "LEFT JOIN foreign_test_table ON FALSE\n" + //
                               "LEFT JOIN test_sequence ON FALSE\n" + //
                               "LEFT JOIN test_view ON FALSE\n" + //
                               "LEFT JOIN test_table_partition ON FALSE\n" + //
                               "LEFT JOIN referencing_table ON FALSE\n" + //
                               "WHERE FALSE;";

        try (Statement statement = connection.createStatement()) {
            createTestingTables(statement);
            statement.execute(queryToBundle);

            String queryId = getQueryIdFromPgStatStatements(statement, "SELECT * FROM test_table%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, queryDiagnosticsParams);

            statement.execute(queryToBundle);

            waitForBundleCompletion(queryId, statement, diagnosticsInterval);

            Path schemaDetailsPath = bundleDataPath.resolve("schema_details.txt");

            assertTrue("schema_details file does not exist", Files.exists(schemaDetailsPath));
            assertGreaterThan("schema_details.txt file is empty",
                              Files.size(schemaDetailsPath) , 0L);

            /* Read the contents of the schema_details.txt file */
            String schemaDetails = new String(Files.readAllBytes(schemaDetailsPath),
                                              StandardCharsets.UTF_8);

            Path expectedOutputPath = Paths.get("src/test/resources/expected/schema_details.out");
            String expectedOutput = new String(Files.readAllBytes(expectedOutputPath),
                                               StandardCharsets.UTF_8);

            assertEquals("schema_details.txt file does not match expected output",
                            expectedOutput.trim(), schemaDetails.trim());
        }
    }

    @Test
    public void testSchemaDetailsDataLimit() throws Exception {
        int diagnosticsInterval = 2;
        QueryDiagnosticsParams queryDiagnosticsParams = new QueryDiagnosticsParams(
            diagnosticsInterval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);
        int MAX_SCHEMA_OIDS = 10;

        try (Statement statement = connection.createStatement()) {
            /* Create (MAX_SCHEMA_OIDS + 1) tables */
            for (int i = 1; i <= (MAX_SCHEMA_OIDS + 1); i++) {
                statement.execute("CREATE TABLE table" + i + "" +
                                  "(id INTEGER PRIMARY KEY, value TEXT)");
            }

            /*
             * Create a query that joins all (MAX_SCHEMA_OIDS + 1) tables.
             * note that table1 appears twice in the query
             */
            StringBuilder queryBuilder = new StringBuilder("SELECT * FROM table1 AS t1");
            for (int i = 1; i <= (MAX_SCHEMA_OIDS + 1); i++) {
                queryBuilder.append(" LEFT JOIN table").append(i)
                            .append(" ON t1.id = table").append(i).append(".id");
            }

            queryBuilder.append(" WHERE FALSE");
            String queryToBundle = queryBuilder.toString();

            statement.execute(queryToBundle);

            String queryId = getQueryIdFromPgStatStatements(statement, "SELECT * FROM table1%");
            Path bundleDataPath = runQueryDiagnostics(statement, queryId, queryDiagnosticsParams);

            statement.execute(queryToBundle);

            waitForBundleCompletion(queryId, statement, diagnosticsInterval);

            Path schemaDetailsPath = bundleDataPath.resolve("schema_details.txt");

            assertTrue("schema_details file does not exist",
                       Files.exists(schemaDetailsPath));
            assertGreaterThan("schema_details.txt file is empty",
                              Files.size(schemaDetailsPath), 0L);

            String schemaDetails = new String(Files.readAllBytes(schemaDetailsPath),
                                              StandardCharsets.UTF_8);
            /* Count the number of table sections in the schema details */
            int tableCount = countTableSections(schemaDetails);

            /* Assert that only MAX_SCHEMA_OIDS tables are outputted */
            assertEquals("Expected " + MAX_SCHEMA_OIDS + "tables in schema details, but found " +
                         tableCount, MAX_SCHEMA_OIDS, tableCount);

            /*
             * Verify that tables from 1 to MAX_SCHEMA_OIDS are present and
             * (MAX_SCHEMA_OIDS + 1) table is absent.
             */
            for (int i = 1; i <= MAX_SCHEMA_OIDS; i++) {
                assertTrue("Table" + i + " is missing from schema details",
                            schemaDetails.contains("Table name: table" + i));
            }
            assertFalse("Table" + (MAX_SCHEMA_OIDS + 1) +
                        " should not be present in schema details",
                        schemaDetails.contains("Table name: table" + (MAX_SCHEMA_OIDS + 1)));

            /*
             * 'table1' was included multiple times in the SELECT query and
             * we want to ensure that the output is unique, so we assert that it appears only once.
             */
            long table1Count = Arrays.stream(schemaDetails.split("\n"))
                .filter(line -> line.equals("Table name: table1"))
                .count();
            assertEquals("Table1 should appear only once in schema details", 1, table1Count);
        }
    }

    /*
     * Verifies the functionality of the yb_cancel_query_diagnostics() function.
     *
     * This test creates three query diagnostics bundles
     * with intervals of 1, 5, and 10 seconds, respectively.
     *
     * The second bundle is cancelled after the first bundle expires.
     * This is verified by checking the yb_query_diagnostics_status entry.
     * Further first bundle is verified to be successfully completed,
     * whereas the 3rd one is still in progress.
     * Later we wait for the 3rd bundle to expire and assert that it is successfully completed.
     */
    @Test
    public void testYbCancelQueryDiagnostics() throws Exception {
        int bundle1Interval = 1;
        int bundle2Interval = 5;
        int bundle3Interval = 10;

        QueryDiagnosticsParams params1 = new QueryDiagnosticsParams(
            bundle1Interval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        QueryDiagnosticsParams params2 = new QueryDiagnosticsParams(
            bundle2Interval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        QueryDiagnosticsParams params3 = new QueryDiagnosticsParams(
            bundle3Interval,
            100 /* explainSampleRate */,
            true /* explainAnalyze */,
            true /* explainDist */,
            false /* explainDebug */,
            0 /* bindVarQueryMinDuration */);

        try (Statement statement = connection.createStatement()) {
            /* Run query diagnostics on the prepared stmt */
            String queryId1 = String.valueOf((int) (Math.random() * 1000));
            String queryId2 = getQueryIdFromPgStatStatements(statement, "PREPARE%");
            String queryId3 = String.valueOf((int) (Math.random() * 1000));

            /* Start processing query diagnostics bundles */
            Path bundleDataPath1 = runQueryDiagnostics(statement, queryId1, params1);
            Path bundleDataPath2 = runQueryDiagnostics(statement, queryId2, params2);
            Path bundleDataPath3 = runQueryDiagnostics(statement, queryId3, params3);

            /* Wait for the 1st bundle to expire */
            Thread.sleep((bundle1Interval * 1000) + BG_WORKER_INTERVAL_MS);

            /* Cancel further processing of the 2nd bundle */
            statement.execute("SELECT yb_cancel_query_diagnostics('" + queryId2 + "')");

            /* Check the cancelled entry within the yb_query_diagnostics_status view */
            QueryDiagnosticsStatus bundleViewEntry2 = getViewData(statement, queryId2, "");

            QueryDiagnosticsStatus expectedBundleViewEntry2 = new QueryDiagnosticsStatus(
                bundleDataPath2, "Cancelled", "Bundle was cancelled", params2);

            assertQueryDiagnosticsStatus(expectedBundleViewEntry2, bundleViewEntry2);

            /* Ensure cancelling 2nd bundle does not affect the functioning of other bundles */

            /* 1st bundle has already expired */
            QueryDiagnosticsStatus bundleViewEntry1 = getViewData(statement, queryId1, "");

            QueryDiagnosticsStatus expectedBundleViewEntry1 = new QueryDiagnosticsStatus(
                bundleDataPath1, "Success", noQueriesExecutedWarning, params1);

            /* verify that the bundle's diagnostics completed successfully */
            assertQueryDiagnosticsStatus(expectedBundleViewEntry1, bundleViewEntry1);

            /* 3rd bundle must be in progress */
            QueryDiagnosticsStatus bundleViewEntry3 = getViewData(statement, queryId3, "");

            QueryDiagnosticsStatus expectedBundleViewEntry3 = new QueryDiagnosticsStatus(
                bundleDataPath3, "In Progress", "", params3);

            assertQueryDiagnosticsStatus(expectedBundleViewEntry3, bundleViewEntry3);

            /* Wait for the 3rd bundle to expire */
            Thread.sleep((bundle3Interval * 1000) + BG_WORKER_INTERVAL_MS);

            /* 3rd bundle must have successfully completed */
            bundleViewEntry3 = getViewData(statement, queryId3, "");

            expectedBundleViewEntry3 = new QueryDiagnosticsStatus(
                bundleDataPath3, "Success", noQueriesExecutedWarning, params3);

            assertQueryDiagnosticsStatus(expectedBundleViewEntry3, bundleViewEntry3);
        }
    }
}

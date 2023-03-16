// Copyright (c) YugaByte, Inc.
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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;

/*
 * Tests whether users can access local file servers when --ysql_disable_server_file_access = true.
 */
@RunWith(value = YBTestRunner.class)
public class TestServerFileAccess extends BasePgSQLTest {
  private static final String GFLAG_ERROR_MESSAGE = "ERROR: server file access disabled";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_disable_server_file_access", "true");
    return flagMap;
  }

  @Test
  public void testFileAccessFunctions() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE su SUPERUSER");

      // Test pg_ls_dir.
      statement.execute("GRANT EXECUTE ON FUNCTION pg_ls_dir(text) TO PUBLIC");
      withRole(statement, "su", () -> {
        runInvalidQuery(statement, "SELECT pg_ls_dir('/non/existent/file')", GFLAG_ERROR_MESSAGE);
      });

      // Test pg_ls_logdir.
      statement.execute("GRANT EXECUTE ON FUNCTION pg_ls_logdir(OUT name text, OUT size bigint, "
          + "OUT modification timestamp with time zone) TO PUBLIC");
      withRole(statement, "su", () -> {
        runInvalidQuery(statement, "SELECT pg_ls_logdir()", GFLAG_ERROR_MESSAGE);
      });

      // Test pg_ls_waldir.
      statement.execute("GRANT EXECUTE ON FUNCTION pg_ls_waldir(OUT name text, OUT size bigint, "
          + "OUT modification timestamp with time zone) TO PUBLIC");
      withRole(statement, "su", () -> {
        runInvalidQuery(statement, "SELECT pg_ls_waldir()", GFLAG_ERROR_MESSAGE);
      });

      // Test pg_read_file.
      statement.execute("GRANT EXECUTE ON FUNCTION pg_read_file(text) TO PUBLIC");
      withRole(statement, "su", () -> {
        runInvalidQuery(statement, "SELECT pg_read_file('/non/existent/file')",
            GFLAG_ERROR_MESSAGE);
      });

      // Test pg_read_binary_file.
      statement.execute("GRANT EXECUTE ON FUNCTION pg_read_binary_file(text) TO PUBLIC");
      withRole(statement, "su", () -> {
        runInvalidQuery(statement, "SELECT pg_read_binary_file('/non/existent/file')",
            GFLAG_ERROR_MESSAGE);
      });

      // Test pg_stat_file.
      statement.execute("GRANT EXECUTE ON FUNCTION pg_stat_file(filename text, missing_ok boolean, "
          + "OUT size bigint, OUT access timestamp with time zone, OUT modification timestamp "
          + "with time zone, OUT change timestamp with time zone, OUT creation timestamp with "
          + "time zone, OUT isdir boolean) TO PUBLIC");
      withRole(statement, "su", () -> {
        runInvalidQuery(statement, "SELECT pg_stat_file('/non/existent/file')",
            GFLAG_ERROR_MESSAGE);
      });
    }
  }

  @Test
  public void testCopyFunctions() throws Exception {
    try (Statement statement = connection.createStatement()) {
      // Test pg_read_server_files role and associated COPY functions.
      // Create table to copy into.
      statement.execute("CREATE TABLE copy_test(id int)");
      statement.execute("GRANT ALL ON TABLE copy_test TO PUBLIC");

      // pg_read_server_files role cannot read files
      withRole(statement, "pg_read_server_files", () -> {
        // Cannot copy from files.
        runInvalidQuery(statement, "COPY copy_test FROM '/dev/null'", GFLAG_ERROR_MESSAGE);
      });

      // Test pg_write_server_files and associated COPY functions.
      withRole(statement, "pg_write_server_files", () -> {
        // Cannot copy to files.
        runInvalidQuery(statement, "COPY copy_test TO '/dev/null'",
            GFLAG_ERROR_MESSAGE);
      });

      // Test pg_execute_server_program and associated COPY functions.
      withRole(statement, "pg_execute_server_program", () -> {
        // Cannot execute a command on the server.
        runInvalidQuery(statement, "COPY copy_test FROM PROGRAM 'ls /usr/bin'",
            GFLAG_ERROR_MESSAGE);
      });
    }
  }

  @Test
  public void testFileAccessInExtensions() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE ROLE su LOGIN SUPERUSER");
    }

    try (Connection connection = getConnectionBuilder().withUser("su").connect();
         Statement statement = connection.createStatement()) {
      // Test file_fdw extension
      statement.execute("CREATE EXTENSION file_fdw");
      statement.execute("CREATE SERVER s1 FOREIGN DATA WRAPPER file_fdw");

      String[] file_fdw_commands = {
        // Cannot create foreign tables from file.
        "CREATE FOREIGN TABLE test (x int) SERVER s1 OPTIONS (filename 'foo');",
        // Cannot create foreign tables from program.
        "CREATE FOREIGN TABLE test (x int) SERVER s1 OPTIONS (program 'foo');",
      };
      runCommandsHelper(statement, file_fdw_commands);

      // Test adminpack extension
      statement.execute("CREATE EXTENSION adminpack");

      String[] adminpack_commands = {
        "SELECT pg_file_write('tmp.txt', 'line', 'false')",
        "SELECT pg_file_rename('a.txt', 'b.txt')",
        "SELECT pg_file_unlink('tmp.txt')",
        "SELECT pg_logdir_ls()",
        // TODO: pg_file_sync was introduced in Postgres 13
        // "SELECT pg_file_sync('tmp.txt');",
      };
      runCommandsHelper(statement, adminpack_commands);

      // Test orafce extension
      statement.execute("CREATE EXTENSION orafce");
      statement.execute("INSERT INTO utl_file.utl_file_dir VALUES ('/tmp/test_dir', 'test_dir')");

      String[] orafce_commands = {
        // Cannot open file.
        "SELECT utl_file.fopen('test_dir', 'test.txt', 'r')",
        // Cannot check if file is opened.
        "SELECT utl_file.is_open(NULL)",
        // Cannot read one line from file.
        "SELECT utl_file.get_line(NULL)",
        // Cannot read one line or return NULL from file.
        "SELECT utl_file.get_nextline(NULL)",
        // Cannot put buffer to file.
        "SELECT utl_file.put(NULL, 'A')",
        // Cannot put line to file.
        "SELECT utl_file.put_line(NULL, 'some_text')",
        // Cannot put new line chars to file.
        "SELECT utl_file.new_line(NULL)",
        // Cannot put formatted text to file.
        "SELECT utl_file.putf(NULL, '[1=%s, 2=%s, 3=%s]', '1', '2', '3')",
        // Cannot flush all data from buffers.
        "SELECT utl_file.fflush(NULL)",
        // Cannot close file.
        "SELECT utl_file.fclose(NULL)",
        // Cannot close all files.
        "SELECT utl_file.fclose_all()",
        // Cannot remove file.
        "SELECT utl_file.fremove('test_dir', 'test.txt')",
        // Cannot rename file.
        "SELECT utl_file.frename('test_dir', 'src_file.txt', 'test_dir', 'dest_file.txt')",
         // Cannot copy text file.
         "SELECT utl_file.fcopy('test_dir', 'src_file.txt', 'test_dir', 'dest_file.txt');",
         // Cannot get file attributes.
         "SELECT utl_file.fgetattr('test_dir', 'test.txt')",
         // Cannot get path of temp directory.
         "SELECT utl_file.tmpdir()"
      };
      runCommandsHelper(statement, orafce_commands);
    }
  }

  protected void runCommandsHelper(Statement statement, String[] commands) {
    for (String command : commands) {
      runInvalidQuery(statement, command, GFLAG_ERROR_MESSAGE);
    }
  }
}

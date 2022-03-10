package org.yb.pgsql;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestAsyncFlush extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestAsyncFlush.class);

  private void createDataFile(String absFilePath, int totalLines) throws IOException {
    File file = new File(absFilePath);
    file.createNewFile();

    BufferedWriter writer = new BufferedWriter(new FileWriter(file));
    writer.write("a,b,c\n");

    for (int i = 0; i < totalLines; i++) {
      writer.write(i+","+i+","+i+"\n");
    }
    writer.close();
  }

  @Test
  public void testCopyWithAsyncFlush() throws Exception {
    String absFilePath = TestUtils.getBaseTmpDir() + "/copy-async-flush.txt";
    String tableName = "copyAsyncFlush";
    int totalLines = 100000;

    createDataFile(absFilePath, totalLines);

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format("CREATE TABLE %s (a int PRIMARY KEY, b int, c int)",
                                      tableName));
      statement.execute(String.format(
          "COPY %s FROM \'%s\' WITH (FORMAT CSV, HEADER)",
          tableName, absFilePath));

      // Verify row count.
      assertOneRow(statement, "SELECT COUNT(*) FROM " + tableName, totalLines);

      // Verify specific rows are present.
      assertOneRow(statement, "SELECT * FROM " + tableName + " WHERE a=0", 0, 0, 0);
      assertOneRow(statement, "SELECT * FROM " + tableName + " WHERE a=50000", 50000, 50000, 50000);
      assertOneRow(statement, "SELECT * FROM " + tableName + " WHERE a=99999", 99999, 99999, 99999);
    }
  }
}

// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.sample;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.lang.InterruptedException;
import java.util.List;
import java.util.stream.Collectors;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

// LargeScan.
// This module insert a larger number of rows and query them in smaller batches.
// To install and execute
//   mvn install exec:java -Dexec.mainClass=org.yb.sample.LargeScan
public class LargeScan {
  private static final Logger LOG = LoggerFactory.getLogger(LargeScan.class);

  private static int rowCount = 4000;

  private static void createTableUsers(Statement stmt, YbSqlUtil yb) throws Exception {
    yb.exec(stmt,
            "CREATE TABLE IF NOT EXISTS users" +
            "  (id text, ename text, age int, city text, about_me text, PRIMARY KEY(id, ename))");

    String insertFormat = "INSERT INTO users VALUES ('%s', '%s', %d, '%s', '%s')";
    for (int iter = 0; iter < rowCount; iter++) {
      String id = String.format("user-%04096d", iter);
      String ename = String.format("name-%d", iter);
      int age = 20 + iter%50;
      String city = String.format("city-%d", iter%1000);
      String aboutMe = String.format("about_me-%d", iter);

      yb.exec(stmt, String.format(insertFormat, id, ename, age, city, aboutMe));
    }
  }

  public static void main(String[] args) throws Exception {
    YbSqlUtil yb = new YbSqlUtil();

    // Connect to local YB database.
    Connection cxn = yb.connectLocal();

    // Get the process id for debugging purpose.
    ResultSet pidResult = yb.execQuery("SELECT pg_backend_pid()");
    pidResult.next();
    LOG.info(String.format("SELECT process ID = %d", pidResult.getInt(1)));
    pidResult.close();

    try {
      // Setup large table "users" if needed.
      if (!yb.tableExists("users")) {
        Statement createStmt = cxn.createStatement();
        createTableUsers(createStmt, yb);
      }

      // Start transaction.
      cxn.setAutoCommit(false);
      yb.exec("BEGIN");

      // Read data from table.
      try (Statement selectStmt = cxn.createStatement()) {
        selectStmt.setFetchSize(100);
        ResultSet rs = yb.execQuery(selectStmt,
            "select yb_mem_usage_sql_b(), id, ename, age, city from users");

        int rowCount = 0;
        while (rs.next()) {
          // Print result every 500.
          rowCount++;
          if (rowCount % 500 == 0) {
            LOG.info(String.format("Row %d: usage = %d bytes," +
                                   " ename = '%s', age = '%s', city = '%s'",
                                   rowCount, rs.getLong(1),
                                   rs.getString(3).trim(), rs.getString(4), rs.getString(5)));
          }
        }
        rs.close();
      }

      // Close transaction.
      yb.exec("END");

    } catch (Exception e) {
      yb.exec("ABORT");
      LOG.info("Failed to execute LargeScan. " + e.getMessage());
    }
  }
}

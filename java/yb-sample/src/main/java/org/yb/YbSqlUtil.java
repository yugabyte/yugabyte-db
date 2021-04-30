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

public class YbSqlUtil {
  private final Logger LOG = LoggerFactory.getLogger(YbSqlUtil.class);
  private Connection localCxn = null;

  public Connection connectLocal() throws Exception {
    Class.forName("org.postgresql.Driver");
    String host = "localhost";
    String connectString = "jdbc:postgresql://" + host + ":5433/yugabyte";
    localCxn = DriverManager.getConnection(connectString, "yugabyte", "yugabyte");

    return localCxn;
  }

  private void checkValidConnection() throws Exception {
    if (localCxn == null) {
      throw new Exception(String.format("Not connected to database"));
    }
  }

  public void exec(String cmd) throws Exception {
    checkValidConnection();
    Statement stmt = localCxn.createStatement();
    exec(stmt, cmd);
  }

  public void exec(Statement stmt, String cmd) throws Exception {
    try {
      stmt.execute(cmd);
    } catch  (SQLException e) {
      LOG.info("SQL " + e.getMessage());
      throw new Exception(String.format("Failed to execute %s", cmd));
    }
  }

  public ResultSet execQuery(String cmd) throws Exception {
    checkValidConnection();
    Statement stmt = localCxn.createStatement();
    return execQuery(stmt, cmd);
  }

  public ResultSet execQuery(Statement stmt, String cmd) throws Exception {
    try {
      return stmt.executeQuery(cmd);
    } catch  (SQLException e) {
      LOG.info("SQL Query " + e.getMessage());
      throw new Exception(String.format("Failed to execute %s", cmd));
    }
  }

  public boolean tableExists(String tableName) throws Exception {
    checkValidConnection();
    try {
      String cmd = String.format("SELECT EXISTS (SELECT FROM pg_tables WHERE tablename = '%s')",
                                 tableName);
      ResultSet rs = execQuery(cmd);
      rs.next();
      return (!rs.wasNull() && rs.getBoolean(1));

    } catch  (SQLException e) {
      LOG.info("SQL Query " + e.getMessage());
      throw new Exception(String.format("Failed to check existence for '%s'", tableName));
    }
  }

  // This function assumes Python script is located in the "src/main/resources".
  public List<String> runPython(String pyName) throws Exception {
    String pyFullName = getFullResourcePath(pyName);
    LOG.info(String.format("Run python script `%s`", pyFullName));

    ProcessBuilder pBuilder = new ProcessBuilder("python", pyFullName);
    pBuilder.redirectErrorStream(true);

    Process p = pBuilder.start();
    List<String> results;
    BufferedReader output = new BufferedReader(new InputStreamReader(p.getInputStream()));
    results = output.lines().collect(Collectors.toList());
    int exitCode = p.waitFor();

    for (String result : results) {
      LOG.info(result);
    }
    if (exitCode != 0) {
      throw new IllegalArgumentException(String.format("Cannot execute '%s'", pyName));
    }
    return results;
  }

  private String getFullResourcePath(String pyName) {
    File file = new File("src/main/resources/" + pyName);
    return file.getAbsolutePath();
  }
}

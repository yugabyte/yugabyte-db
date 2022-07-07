// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.sample;
import org.yb.sample.YbSqlUtil;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.sql.ResultSet;
import java.sql.SQLException;

// SimpleScan.
// This module insert a few rows and read them back.
// To install and execute
//   mvn install exec:java -Dexec.mainClass=org.yb.sample.SimpleScan
public class SimpleScan {
  private static final Logger LOG = LoggerFactory.getLogger(SimpleScan.class);

  public static void main(String[] args) throws Exception {
    try {
      YbSqlUtil yb = new YbSqlUtil();

      // Connect to local YB database.
      yb.connectLocal();

      // Clear previous table.
      yb.exec("DROP TABLE IF EXISTS my_table");

      // Create table and insert some rows.
      yb.exec("CREATE TABLE my_table" +
              "  (id text, ename text, age int, city text, about_me text," +
              "   PRIMARY KEY(id, ename))");
      yb.exec("INSERT INTO my_table VALUES('one', 'one', 1, 'one', 'one')");
      yb.exec("INSERT INTO my_table VALUES('two', 'two', 1, 'two', 'two')");
      yb.exec("INSERT INTO my_table VALUES('three', 'three', 1, 'three', 'three')");

      // Read data from table.
      int rowCount = 0;
      ResultSet rs = yb.execQuery("select id, ename, age, city from my_table");
      while (rs.next()) {
        LOG.info(String.format("Row %d: ename = '%s', age = '%s', city = '%s'",
                               ++rowCount, rs.getString(2), rs.getString(3), rs.getString(4)));
      }
      rs.close();

    } catch (SQLException e) {
      LOG.info("Failed to execute SimpleScan");
    } catch (Exception e) {
      LOG.info("Failed to execute SimpleScan. " + e.getMessage());
    }
  }
}

package org.yb.cql;

import java.net.InetAddress;
import org.junit.Test;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.junit.Assert.assertEquals;

public class TestSystemTables extends TestBase {

  private void verifyPeersTable(ResultSet rs, InetAddress expected) throws Exception {
    assertEquals(1, rs.all().size());
    for (Row row : rs.all()) {
      assertEquals(expected, row.getInet(0)); // peer
      assertEquals(expected, row.getInet(6)); // rpc address
    }
  }

  @Test
  public void testSystemPeersTable() throws Exception {
    // Pick only 1 contact point since all will have same IP.
    InetAddress contactPoint = miniCluster.getCQLContactPoints().get(0).getAddress();

    ResultSet rs = session.execute("SELECT * FROM system.peers;");
    verifyPeersTable(rs, contactPoint);

    // Try with where clause.
    rs = session.execute(String.format("SELECT * FROM system.peers WHERE peer = '%s'",
      contactPoint.getHostAddress()));
    verifyPeersTable(rs, contactPoint);

    rs = session.execute("SELECT * FROM system.peers WHERE peer = '127.0.0.2'");
    assertEquals(0, rs.all().size());
  }

  @Test
  public void testSystemAggregatesTable() throws Exception {
    // Table should be empty.
    assertEquals(0, session.execute("SELECT * FROM system_schema.aggregates;").all().size());
  }
}

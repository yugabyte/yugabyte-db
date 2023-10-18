package org.yb.pgsql;

import java.sql.Statement;

import java.util.Map;

import org.yb.util.json.Checker;

class BasePgExplainAnalyzeTest extends BasePgSQLTest {
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_prefetch_limit", "1024");
    flagMap.put("ysql_session_max_batch_size", "512");
    return flagMap;
  }

  protected void testExplain(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ExplainAnalyzeUtils.testExplain(stmt, query, checker);
    }
  }
}

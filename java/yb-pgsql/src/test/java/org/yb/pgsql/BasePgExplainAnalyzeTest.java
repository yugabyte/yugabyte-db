package org.yb.pgsql;

import java.sql.Statement;

import java.util.Map;

import org.yb.util.json.Checker;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

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

  protected void testExplainDebug(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ExplainAnalyzeUtils.testExplainDebug(stmt, query, checker);
    }
  }

  public static ExplainAnalyzeUtils.Cost getExplainTotalCost(Statement stmt, String query)
  throws Exception {
      return ExplainAnalyzeUtils.getExplainTotalCost(stmt, query);
  }

  public static ExplainAnalyzeUtils.Cost getExplainTotalCost(String query)
  throws Exception {
    try (Statement stmt = connection.createStatement()) {
      return ExplainAnalyzeUtils.getExplainTotalCost(stmt, query);
    }
  }
}

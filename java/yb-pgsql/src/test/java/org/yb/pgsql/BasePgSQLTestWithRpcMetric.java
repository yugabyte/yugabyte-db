package org.yb.pgsql;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import org.yb.minicluster.Metrics;

import java.util.HashMap;
import java.util.Map;

import static org.yb.AssertionWrappers.assertEquals;

public class BasePgSQLTestWithRpcMetric  extends BasePgSQLTest {

  // Start server in RF=1 mode to simplify metrics analysis.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("export_intentdb_metrics", "true");
    return flagMap;
  }

  protected static class Counter {
    private int previousValue_ = 0;
    private int currentValue_ = 0;

    public void update(int newValue) {
      previousValue_ = currentValue_;
      currentValue_ = newValue;
    }

    public int value() {
      return currentValue_ - previousValue_;
    }
  }

  protected static class OperationsCounter {
    public Map<String, Counter> tableWrites = new HashMap<>();
    public Map<String, Counter> intentdbSeeks = new HashMap<>();

    public Counter rpc = new Counter();

    public OperationsCounter(String... tableNames) {
      for (String table : tableNames) {
        tableWrites.put(table, new Counter());
        intentdbSeeks.put(table, new Counter());
      }
    }
  }

  protected OperationsCounter updateCounter(OperationsCounter counter) throws Exception {
    JsonArray[] metrics = getRawTSMetric();
    assertEquals(1, metrics.length);
    for (JsonElement el : metrics[0]) {
      JsonObject obj = el.getAsJsonObject();
      String metricType = obj.get("type").getAsString();
      if (metricType.equals("server") && obj.get("id").getAsString().equals("yb.tabletserver")) {
        counter.rpc.update(new Metrics(obj).getCounter("rpc_inbound_calls_created").value);
      } else if (metricType.equals("tablet")) {
        // Assume each table has single tablet, for this purposes PRIMARY KEY(k ASC) is used.
        String tableName = obj.getAsJsonObject("attributes").get("table_name").getAsString();
        Counter writes = counter.tableWrites.get(tableName);
        if (writes != null) {
          writes.update(new Metrics(obj).getCounter("intentsdb_rocksdb_write_self").value);
        }

        Counter seeks = counter.intentdbSeeks.get(tableName);
        if (seeks != null) {
          seeks.update(new Metrics(obj).getCounter("intentsdb_rocksdb_number_db_seek").value);
        }
      }
    }
    return counter;
  }
}

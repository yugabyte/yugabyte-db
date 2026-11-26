// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import com.yugabyte.replication.PGReplicationConnection;
import com.yugabyte.replication.PGReplicationStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.pgsql.PgOutputMessageDecoder.PgOutputMessage;

/** Shared setup and decode helpers for logical replication slot tests. */
final class PgReplicationSlotTestUtil {
  private static final Logger LOG = LoggerFactory.getLogger(PgReplicationSlotTestUtil.class);

  static final String YB_OUTPUT_PLUGIN_NAME = "yboutput";
  static final int NUM_TSERVERS = 3;
  static final int PUBLICATION_REFRESH_INTERVAL_SEC = 5;

  private PgReplicationSlotTestUtil() {
  }

  static void addCommonTServerFlags(Map<String, String> flagMap) {
    if (BasePgSQLTest.isTestRunningWithConnectionManager()) {
      flagMap.put("ysql_conn_mgr_stats_interval", "1");
    }
    flagMap.put(
        "cdcsdk_publication_list_refresh_interval_secs",
        Integer.toString(PUBLICATION_REFRESH_INTERVAL_SEC));
    flagMap.put("cdc_send_null_before_image_if_not_exists", "true");
    flagMap.put("TEST_dcheck_for_missing_schema_packing", "false");
    flagMap.put("ysql_cdc_active_replication_slot_window_ms", "0");
  }

  static void addCommonMasterFlags(Map<String, String> flagMap) {
    flagMap.put("TEST_dcheck_for_missing_schema_packing", "false");
  }

  static void createSlot(PGReplicationConnection replConnection, String slotName,
      String pluginName) throws Exception {
    replConnection.createReplicationSlot()
        .logical()
        .withSlotName(slotName)
        .withOutputPlugin(pluginName)
        .make();
  }

  static List<PgOutputMessage> receiveMessage(PGReplicationStream stream, int count)
      throws Exception {
    List<PgOutputMessage> result = new ArrayList<>(count);
    for (int index = 0; index < count; index++) {
      PgOutputMessage message = PgOutputMessageDecoder.DecodeBytes(stream.read());
      result.add(message);
      LOG.info("Row = {}", message);
    }
    return result;
  }
}

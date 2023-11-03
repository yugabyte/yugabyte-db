// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TableDetails;
import java.util.UUID;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetTableSchemaResponse;

public class TableDefinitionTaskParams extends TableTaskParams {
  public TableType tableType = null;
  public TableDetails tableDetails = null;

  public static TableDefinitionTaskParams createFromResponse(
      Universe universe, UUID tableUUID, GetTableSchemaResponse response) {

    // Verify tableUUID is correct
    String noDashTableUUID = tableUUID.toString().replace("-", "");
    if (!noDashTableUUID.equals(response.getTableId())) {
      throw new IllegalArgumentException(
          "UUID of table in schema ("
              + noDashTableUUID
              + ") did not match UUID of table in request ("
              + response.getTableId()
              + ").");
    }

    TableDefinitionTaskParams params = new TableDefinitionTaskParams();

    params.setUniverseUUID(universe.getUniverseUUID());
    params.expectedUniverseVersion = universe.getUniverseDetails().expectedUniverseVersion;
    params.tableUUID = tableUUID;
    params.tableType = response.getTableType();
    params.tableDetails = TableDetails.createWithSchema(response.getSchema());
    params.tableDetails.tableName = response.getTableName();
    params.tableDetails.keyspace = response.getNamespace();

    return params;
  }
}

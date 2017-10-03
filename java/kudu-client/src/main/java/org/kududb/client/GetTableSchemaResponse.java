// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import org.kududb.Schema;
import org.kududb.annotations.InterfaceAudience;

@InterfaceAudience.Private
public class GetTableSchemaResponse extends KuduRpcResponse {

  private final Schema schema;
  private final PartitionSchema partitionSchema;
  private final boolean createTableDone;
  private final String tableId;

  /**
   * @param ellapsedMillis Time in milliseconds since RPC creation to now
   * @param schema the table's schema
   * @param partitionSchema the table's partition schema
   */
  GetTableSchemaResponse(long ellapsedMillis,
                         String tsUUID,
                         Schema schema,
                         String tableId,
                         PartitionSchema partitionSchema,
                         boolean createTableDone) {
    super(ellapsedMillis, tsUUID);
    this.schema = schema;
    this.partitionSchema = partitionSchema;
    this.createTableDone = createTableDone;
    this.tableId = tableId;
  }

  /**
   * Get the table's schema.
   * @return Table's schema
   */
  public Schema getSchema() {
    return schema;
  }

  /**
   * Get the table's partition schema.
   * @return the table's partition schema
   */
  public PartitionSchema getPartitionSchema() {
    return partitionSchema;
  }

  /**
   * Tells if the original CreateTable call has completed and the tablets are ready.
   * @return true if the table is created, otherwise false
   */
  public boolean isCreateTableDone() {
    return createTableDone;
  }

  /**
   * Get the table's unique identifier.
   * @return the table's tableId
   */
  public String getTableId() {
    return tableId;
  }
}

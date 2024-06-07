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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import com.google.common.annotations.VisibleForTesting;
import org.yb.CommonTypes.TableType;
import org.yb.IndexInfo;
import org.yb.Schema;
import org.yb.annotations.InterfaceAudience;

import java.util.List;

@InterfaceAudience.Private
public class GetTableSchemaResponse extends YRpcResponse {

  private final Schema schema;
  private final PartitionSchema partitionSchema;
  private final boolean createTableDone;
  private final String namespace;
  private final String tableName;
  private final String tableId;
  private final TableType tableType;
  private final List<IndexInfo> indexes;

  private final boolean colocated;

  /**
   * @param ellapsedMillis Time in milliseconds since RPC creation to now
   * @param schema the table's schema
   * @param partitionSchema the table's partition schema
   */
  @VisibleForTesting
  public GetTableSchemaResponse(
      long ellapsedMillis,
      String tsUUID,
      Schema schema,
      String namespace,
      String tableName,
      String tableId,
      PartitionSchema partitionSchema,
      boolean createTableDone,
      TableType tableType,
      List<IndexInfo> indexes,
      boolean colocated) {
    super(ellapsedMillis, tsUUID);
    this.schema = schema;
    this.partitionSchema = partitionSchema;
    this.createTableDone = createTableDone;
    this.namespace = namespace;
    this.tableName = tableName;
    this.tableId = tableId;
    this.tableType = tableType;
    this.indexes = indexes;
    this.colocated = colocated;
  }

  /**
   * Get the table's schema.
   * @return Table's schema
   */
  public Schema getSchema() {
    return schema;
  }

  /**
   * Get the table's indexes.
   * @return Table's indexes
   */
  public List<IndexInfo> getIndexes() {
    return indexes;
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
   * Get the table's namespace.
   * @return the table's namespace.
   */
  public String getNamespace() {
    return namespace;
  }

  /**
   * Get whether table is colocated or not.
   * @return true if table is colocated, false otherwise
   */
  public boolean isColocated() {
    return this.colocated;
  }

  /**
   * Get the table's name.
   * @return the table's name
   */
  public String getTableName() {
    return tableName;
  }

  /**
   * Get the table's unique identifier.
   * @return the table's tableId
   */
  public String getTableId() {
    return tableId;
  }

  /**
   * Get the table type.
   * @return the table's type.
   */
  public TableType getTableType() {
    return tableType;
  }
}

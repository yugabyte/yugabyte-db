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

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.master.MasterDdlOuterClass;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.ArrayList;
import java.util.stream.Collectors;
import java.util.stream.Stream;

@InterfaceAudience.Public
@InterfaceStability.Evolving
public class ListTablesResponse extends YRpcResponse {

  private List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesList;

  ListTablesResponse(long ellapsedMillis,
                     String tsUUID,
                     List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tablesList) {
    super(ellapsedMillis, tsUUID);
    this.tablesList = tablesList;
  }

  /**
   * Get the list of tables as specified in the request.
   * @return a list of table info
   */
  public List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> getTableInfoList() {
    return tablesList;
  }

  /**
   * Get the list of tables as specified in the request.
   * @return a list of table names
   */
  public List<String> getTablesList() {
    int serversCount = tablesList.size();
    List<String> tables = new ArrayList<String>(serversCount);
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo info : tablesList) {
      tables.add(info.getName());
    }
    return tables;
  }

  /**
   * Merges tablesList from input ListTablesResponse into this tablesList.
   * @return this instance
   */
  public ListTablesResponse mergeWith(ListTablesResponse inputResponse) {
    if (inputResponse == null) { return this; }
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> inputTablesList =
        inputResponse.getTableInfoList();
    if (inputTablesList != null && !inputTablesList.isEmpty()) {
      // Protobuf returns an unmodifiable list, so we need a new ArrayList.
      inputTablesList = new ArrayList<>(inputTablesList);
      if (this.tablesList != null && !this.tablesList.isEmpty()) {
        inputTablesList.addAll(this.tablesList);
      }
      this.tablesList = inputTablesList;
    }
    return this;
  }

}

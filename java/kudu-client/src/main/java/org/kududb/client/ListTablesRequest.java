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

import com.google.protobuf.Message;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.master.Master;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Private
class ListTablesRequest extends KuduRpc<ListTablesResponse> {

  private final String nameFilter;

  ListTablesRequest(KuduTable masterTable, String nameFilter) {
    super(masterTable);
    this.nameFilter = nameFilter;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.ListTablesRequestPB.Builder builder =
        Master.ListTablesRequestPB.newBuilder();
    if (nameFilter != null) {
      builder.setNameFilter(nameFilter);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ListTables";
  }

  @Override
  Pair<ListTablesResponse, Object> deserialize(CallResponse callResponse,
                                               String tsUUID) throws Exception {
    final Master.ListTablesResponsePB.Builder respBuilder =
        Master.ListTablesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    int serversCount = respBuilder.getTablesCount();
    List<String> tables = new ArrayList<String>(serversCount);
    for (Master.ListTablesResponsePB.TableInfo info : respBuilder.getTablesList()) {
      tables.add(info.getName());
    }
    ListTablesResponse response = new ListTablesResponse(deadlineTracker.getElapsedMillis(),
                                                         tsUUID, tables);
    return new Pair<ListTablesResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

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
import static org.kududb.master.Master.*;

import org.kududb.annotations.InterfaceAudience;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Private
public class ListTabletServersRequest extends KuduRpc<ListTabletServersResponse> {

  public ListTabletServersRequest(KuduTable masterTable) {
    super(masterTable);
  }
  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final ListTabletServersRequestPB.Builder builder =
        ListTabletServersRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ListTabletServers";
  }

  @Override
  Pair<ListTabletServersResponse, Object> deserialize(CallResponse callResponse,
                                                      String tsUUID) throws Exception {
    final ListTabletServersResponsePB.Builder respBuilder =
        ListTabletServersResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    int serversCount = respBuilder.getServersCount();
    List<String> servers = new ArrayList<String>(serversCount);
    for (ListTabletServersResponsePB.Entry entry : respBuilder.getServersList()) {
      servers.add(entry.getRegistration().getRpcAddresses(0).getHost());
    }
    ListTabletServersResponse response = new ListTabletServersResponse(deadlineTracker
        .getElapsedMillis(), tsUUID, serversCount, servers);
    return new Pair<ListTabletServersResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

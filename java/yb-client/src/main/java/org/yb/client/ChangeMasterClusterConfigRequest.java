// Copyright (c) YugaByte, Inc.
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

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import org.yb.util.Pair;

@InterfaceAudience.Public
class ChangeMasterClusterConfigRequest extends YRpc<ChangeMasterClusterConfigResponse> {
  private CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig;

  public ChangeMasterClusterConfigRequest(
      YBTable masterTable, CatalogEntityInfo.SysClusterConfigEntryPB config) {
    super(masterTable);
    clusterConfig = config;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.ChangeMasterClusterConfigRequestPB.Builder builder =
      MasterClusterOuterClass.ChangeMasterClusterConfigRequestPB.newBuilder();
    builder.setClusterConfig(clusterConfig);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ChangeMasterClusterConfig";
  }

  @Override
  Pair<ChangeMasterClusterConfigResponse, Object> deserialize(CallResponse callResponse,
                                                 String masterUUID) throws Exception {
    final MasterClusterOuterClass.ChangeMasterClusterConfigResponsePB.Builder respBuilder =
      MasterClusterOuterClass.ChangeMasterClusterConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    ChangeMasterClusterConfigResponse response =
      new ChangeMasterClusterConfigResponse(deadlineTracker.getElapsedMillis(),
                               masterUUID,
                               hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<ChangeMasterClusterConfigResponse, Object>(response, hasErr ? respBuilder.getError() : null);
  }
}

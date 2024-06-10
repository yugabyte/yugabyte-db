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

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;

import org.yb.annotations.InterfaceAudience;
import org.yb.CommonNet.HostPortPB;
import org.yb.consensus.Consensus;
import org.yb.consensus.Metadata;
import org.yb.consensus.Metadata.RaftPeerPB;
import org.yb.master.MasterClusterOuterClass;
import org.yb.util.Pair;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Public
class GetMasterClusterConfigRequest extends YRpc<GetMasterClusterConfigResponse> {
  public GetMasterClusterConfigRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.GetMasterClusterConfigRequestPB.Builder builder =
      MasterClusterOuterClass.GetMasterClusterConfigRequestPB.newBuilder();

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "GetMasterClusterConfig"; }

  @Override
  Pair<GetMasterClusterConfigResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final MasterClusterOuterClass.GetMasterClusterConfigResponsePB.Builder respBuilder =
      MasterClusterOuterClass.GetMasterClusterConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    GetMasterClusterConfigResponse response =
      new GetMasterClusterConfigResponse(
          deadlineTracker.getElapsedMillis(),
          masterUUID,
          respBuilder.getClusterConfig(),
          hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<GetMasterClusterConfigResponse, Object>(response, hasErr ? respBuilder.getError() : null);
  }
}

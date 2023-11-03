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
import org.yb.master.MasterReplicationOuterClass;
import org.yb.util.Pair;

public class GetDBStreamInfoRequest extends YRpc<GetDBStreamInfoResponse>{

  private final String streamId;

  GetDBStreamInfoRequest(YBTable table, String streamId) {
    super(table);
    this.streamId = streamId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.GetCDCDBStreamInfoRequestPB.Builder builder =
            MasterReplicationOuterClass.GetCDCDBStreamInfoRequestPB.newBuilder();
    builder.setDbStreamId(ByteString.copyFromUtf8(this.streamId));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "GetCDCDBStreamInfo";
  }

  @Override
  Pair<GetDBStreamInfoResponse, Object> deserialize(CallResponse callResponse,
                                                    String tsUUID) throws Exception {
    final MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.Builder respBuilder =
      MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);


    GetDBStreamInfoResponse response =
      new GetDBStreamInfoResponse(deadlineTracker.getElapsedMillis(), tsUUID,
        respBuilder.getTableInfoList(), respBuilder.getNamespaceId().toStringUtf8());
    return new Pair<GetDBStreamInfoResponse, Object>(
      response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

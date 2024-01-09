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
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.GetChangesRequestPB;
import org.yb.cdc.CdcService.GetChangesResponsePB;
import org.yb.util.Pair;

public class GetChangesRequest extends YRpc<GetChangesResponse> {
  private final String streamId;

  public String getTabletId() {
    return tabletId;
  }

  private final String tabletId;
  private final long term;
  private final long index;
  private final byte[] key;
  private final int write_id;
  private final long time;
  private final boolean needSchemaInfo;
  private final CdcSdkCheckpoint explicitCheckpoint;
  private final String tableId;
  private final long safeHybridTime;
  private final int walSegmentIndex;

  public GetChangesRequest(YBTable table, String streamId, String tabletId,
   long term, long index, byte[] key, int write_id, long time, boolean needSchemaInfo) {
    this(table, streamId, tabletId, term, index, key, write_id, time, needSchemaInfo,
         null, new String(""), -1);
  }

  public GetChangesRequest(YBTable table, String streamId, String tabletId, long term, long index,
      byte[] key, int write_id, long time, boolean needSchemaInfo,
      CdcSdkCheckpoint explicitCheckpoint, String tableId, long safeHybridTime) {
    this(table, streamId, tabletId, term, index, key, write_id, time, needSchemaInfo,
        explicitCheckpoint, tableId, safeHybridTime, 0);
  }

  public GetChangesRequest(YBTable table, String streamId, String tabletId, long term, long index,
      byte[] key, int write_id, long time, boolean needSchemaInfo,
      CdcSdkCheckpoint explicitCheckpoint, String tableId, long safeHybridTime,
      int walSegmentIndex) {
    super(table);
    this.streamId = streamId;
    this.tabletId = tabletId;
    this.term = term;
    this.index = index;
    this.key = key;
    this.write_id = write_id;
    this.time = time;
    this.needSchemaInfo = needSchemaInfo;
    this.explicitCheckpoint = explicitCheckpoint;
    this.tableId = tableId;
    this.safeHybridTime = safeHybridTime;
    this.walSegmentIndex = walSegmentIndex;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetChangesRequestPB.Builder builder = GetChangesRequestPB.newBuilder();
    builder.setDbStreamId(ByteString.copyFromUtf8(this.streamId));
    builder.setTabletId(ByteString.copyFromUtf8(this.tabletId));

    if (this.tableId.length() != 0) {
      builder.setTableId(ByteString.copyFromUtf8(this.tableId));
    }

    builder.setNeedSchemaInfo(this.needSchemaInfo);
    if (term != 0 || index != 0) {
      CdcService.CDCSDKCheckpointPB.Builder checkpointBuilder =
              CdcService.CDCSDKCheckpointPB.newBuilder();
      checkpointBuilder.setIndex(this.index).setTerm(this.term)
        .setWriteId(this.write_id)
        .setSnapshotTime(this.time);
      if (key != null) {
        checkpointBuilder.setKey(ByteString.copyFrom(this.key));
      }
      builder.setFromCdcSdkCheckpoint(checkpointBuilder.build());
    }

    if (explicitCheckpoint != null) {
      CdcService.CDCSDKCheckpointPB.Builder checkpointBuilder =
              CdcService.CDCSDKCheckpointPB.newBuilder();
      checkpointBuilder.setIndex(explicitCheckpoint.getIndex())
        .setTerm(explicitCheckpoint.getTerm())
        .setWriteId(explicitCheckpoint.getWriteId()).setSnapshotTime(explicitCheckpoint.getTime());
      if (explicitCheckpoint.getKey() != null) {
        checkpointBuilder.setKey(ByteString.copyFrom(explicitCheckpoint.getKey()));
      }
      builder.setExplicitCdcSdkCheckpoint(checkpointBuilder.build());
    }

    if (safeHybridTime != -1) {
      builder.setSafeHybridTime(safeHybridTime);
    }

    builder.setWalSegmentIndex(walSegmentIndex);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CDC_SERVICE_NAME; }

  @Override
  String method() {
    return "GetChanges";
  }

  @Override
  Pair<GetChangesResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final GetChangesResponsePB.Builder respBuilder = GetChangesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    GetChangesResponse response = new GetChangesResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.build(),
      respBuilder.getCdcSdkCheckpointBuilder().getKey().toByteArray(),
      respBuilder.getCdcSdkCheckpointBuilder().getWriteId());
    return new Pair<GetChangesResponse, Object>(
            response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

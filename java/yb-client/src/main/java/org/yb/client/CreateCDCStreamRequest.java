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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes;
import org.yb.cdc.CdcService.CreateCDCStreamRequestPB;
import org.yb.cdc.CdcService.CreateCDCStreamResponsePB;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;

public class CreateCDCStreamRequest extends YRpc<CreateCDCStreamResponse> {
  public static final Logger LOG = LoggerFactory.getLogger(CreateCDCStreamRequest.class);

  private final String tableId;
  private final String namespaceName;
  private final CdcService.CDCRequestSource source_type;
  private final CdcService.CDCRecordFormat record_format;
  private final CdcService.CDCCheckpointType checkpoint_type;
  private CdcService.CDCRecordType recordType;

  private CommonTypes.YQLDatabase dbType;

  public CreateCDCStreamRequest(YBTable masterTable, String tableId,
                                String namespaceName, String format,
                                String checkpointType, String recordType) {
    super(masterTable);
    this.tableId = tableId;
    this.namespaceName = namespaceName;
    this.source_type = CdcService.CDCRequestSource.CDCSDK;
    if (format.equalsIgnoreCase("PROTO"))
      this.record_format = CdcService.CDCRecordFormat.PROTO;
    else {
      this.record_format = CdcService.CDCRecordFormat.JSON;
    }
    if (checkpointType.equalsIgnoreCase("EXPLICIT")) {
      this.checkpoint_type = CdcService.CDCCheckpointType.EXPLICIT;
    }
    else {
      this.checkpoint_type = CdcService.CDCCheckpointType.IMPLICIT;
    }

    // If record type is null or empty then it will be set as the default value which is CHANGE.
    // For more information on default values, see yugabyte-db/src/yb/cdc/cdc_service.proto.
    if (recordType != null && !recordType.isEmpty()) {
      if (recordType.equalsIgnoreCase("ALL")) {
        this.recordType = CdcService.CDCRecordType.ALL;
      } else if (recordType.equalsIgnoreCase("FULL_ROW_NEW_IMAGE")) {
        this.recordType = CdcService.CDCRecordType.FULL_ROW_NEW_IMAGE;
      } else if (recordType.equalsIgnoreCase("MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES")) {
        this.recordType = CdcService.CDCRecordType.MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES;
      } else if (recordType.equalsIgnoreCase("CHANGE")) {
        this.recordType = CdcService.CDCRecordType.CHANGE;
      } else if (recordType.equalsIgnoreCase("FULL")) {
        this.recordType = CdcService.CDCRecordType.PG_FULL;
      } else if (recordType.equalsIgnoreCase("CHANGE_OLD_NEW")) {
        this.recordType = CdcService.CDCRecordType.PG_CHANGE_OLD_NEW;
      } else if (recordType.equalsIgnoreCase("DEFAULT")) {
        this.recordType = CdcService.CDCRecordType.PG_DEFAULT;
      } else if (recordType.equalsIgnoreCase("NOTHING")) {
        this.recordType = CdcService.CDCRecordType.PG_NOTHING;
      } else {
        throw new IllegalArgumentException("Invalid record type: " + recordType);
      }
    } else {
      this.recordType = CdcService.CDCRecordType.CHANGE;
    }
  }

  public CreateCDCStreamRequest(YBTable masterTable, String tableId,
                                String namespaceName, String format,
                                String checkpointType) {
    this(masterTable, tableId, namespaceName, format, checkpointType, null);
  }

  public CreateCDCStreamRequest(YBTable masterTable, String tableId,
                                String namespaceName, String format,
                                String checkpointType, String recordType,
                                CommonTypes.YQLDatabase dbType) {

    this(masterTable, tableId, namespaceName, format, checkpointType, recordType);
    this.dbType = dbType;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final CreateCDCStreamRequestPB.Builder builder = CreateCDCStreamRequestPB.newBuilder();
    if (namespaceName == null)
      builder.setTableId(this.tableId);
    builder.setNamespaceName(this.namespaceName);
    builder.setSourceType(this.source_type);
    builder.setRecordFormat(this.record_format);
    builder.setCheckpointType(this.checkpoint_type);

    if (recordType != null) {
      builder.setRecordType(this.recordType);
    }

    if (dbType != null) {
      builder.setDbType(this.dbType);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CDC_SERVICE_NAME; }

  @Override
  String method() {
    return "CreateCDCStream";
  }

  @Override
  Pair<CreateCDCStreamResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final CreateCDCStreamResponsePB.Builder respBuilder = CreateCDCStreamResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    CreateCDCStreamResponse response = new CreateCDCStreamResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.getDbStreamId().toStringUtf8());
    return new Pair<CreateCDCStreamResponse, Object>(
            response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

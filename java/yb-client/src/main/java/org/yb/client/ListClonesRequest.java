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
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo.SysCloneStatePB;
import org.yb.master.MasterBackupOuterClass.ListClonesRequestPB;
import org.yb.master.MasterBackupOuterClass.ListClonesResponsePB;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;
import org.yb.util.CloneUtil;

@InterfaceAudience.Public
public class ListClonesRequest extends YRpc<ListClonesResponse> {

  private final String keyspaceId;
  private final Integer cloneSeqNo;

  ListClonesRequest(
      YBTable table,
      String keyspaceId,
      Integer cloneSeqNo) {
    super(table);
    this.keyspaceId = keyspaceId;
    this.cloneSeqNo = cloneSeqNo;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ListClonesRequestPB.Builder builder = ListClonesRequestPB.newBuilder();
    builder.setSourceNamespaceId(keyspaceId);
    builder.setSeqNo(cloneSeqNo);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_BACKUP_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ListClones";
  }

  @Override
  Pair<ListClonesResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final ListClonesResponsePB.Builder respBuilder = ListClonesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    MasterErrorPB serverError = hasErr ? respBuilder.getError() : null;

    List<CloneInfo> cloneInfoList = new LinkedList<>();
    if (!hasErr) {
        for (SysCloneStatePB clone : respBuilder.getEntriesList()) {
            cloneInfoList.add(CloneUtil.parseCloneInfoPB(clone));
        }
    }

    ListClonesResponse response =
        new ListClonesResponse(
            deadlineTracker.getElapsedMillis(),
            masterUUID,
            serverError,
            cloneInfoList);
    return new Pair<ListClonesResponse, Object>(response, serverError);
  }
}

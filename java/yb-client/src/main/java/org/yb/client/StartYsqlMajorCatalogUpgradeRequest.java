// Copyright (c) YugabyteDB, Inc.
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
import org.yb.master.MasterAdminOuterClass;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class StartYsqlMajorCatalogUpgradeRequest
    extends YRpc<StartYsqlMajorCatalogUpgradeResponse> {

  public StartYsqlMajorCatalogUpgradeRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "StartYsqlMajorCatalogUpgrade";
  }

  @Override
  Pair<StartYsqlMajorCatalogUpgradeResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final MasterAdminOuterClass.StartYsqlMajorCatalogUpgradeResponsePB.Builder respBuilder =
        MasterAdminOuterClass.StartYsqlMajorCatalogUpgradeResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    StartYsqlMajorCatalogUpgradeResponse response =
        new StartYsqlMajorCatalogUpgradeResponse(
            deadlineTracker.getElapsedMillis(),
            tsUUID,
            hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<StartYsqlMajorCatalogUpgradeResponse, Object>(
        response, hasErr ? respBuilder.getError() : null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterAdminOuterClass.StartYsqlMajorCatalogUpgradeRequestPB.Builder builder =
        MasterAdminOuterClass.StartYsqlMajorCatalogUpgradeRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }
}

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

package org.yb.client;

import org.yb.util.Pair;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.CatalogEntityInfo.ReplicationInfoPB;
import org.yb.master.MasterReplicationOuterClass.ValidateReplicationInfoResponsePB;

public class ValidateReplicationInfoRequest extends YRpc<ValidateReplicationInfoResponse>{

    private final ReplicationInfoPB replicationInfoPB;

    ValidateReplicationInfoRequest(YBTable table, ReplicationInfoPB replicationInfoPB) {
        super(table);
        this.replicationInfoPB = replicationInfoPB;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        MasterReplicationOuterClass.ValidateReplicationInfoRequestPB.Builder builder =
            MasterReplicationOuterClass.ValidateReplicationInfoRequestPB.newBuilder();
        if(replicationInfoPB != null) {
            builder.setReplicationInfo(replicationInfoPB);
        }
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() {
        return MASTER_SERVICE_NAME;
    }

    @Override
    String method() {
        return "ValidateReplicationInfo";
    }

    @Override
    Pair<ValidateReplicationInfoResponse, Object> deserialize(CallResponse callResponse,
            String uuid) throws Exception{
        final ValidateReplicationInfoResponsePB.Builder respBuilder =
                ValidateReplicationInfoResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        MasterTypes.MasterErrorPB serverError =
                respBuilder.hasError() ? respBuilder.getError() : null;
        ValidateReplicationInfoResponse response =
                new ValidateReplicationInfoResponse(deadlineTracker.getElapsedMillis(),
                        uuid, serverError);
        return new Pair<ValidateReplicationInfoResponse, Object>(response, serverError);
    }

}

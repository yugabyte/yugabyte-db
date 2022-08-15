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

import org.yb.master.MasterReplicationOuterClass;

import java.util.List;

public class GetDBStreamInfoResponse extends YRpcResponse {

    private final List<MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.TableInfo>
            tableInfoList;
    private final String namespaceId;

    public GetDBStreamInfoResponse(long elapsedMillis, String tsUUID,
                                   List<MasterReplicationOuterClass
                                           .GetCDCDBStreamInfoResponsePB.TableInfo>
                                           tableInfoList, String namespaceId) {
        super(elapsedMillis, tsUUID);
        this.tableInfoList = tableInfoList;
        this.namespaceId = namespaceId;
    }

    public List<MasterReplicationOuterClass
            .GetCDCDBStreamInfoResponsePB.TableInfo> getTableInfoList() {
        return tableInfoList;
    }

    public String getNamespaceId() {
        return namespaceId;
    }
}

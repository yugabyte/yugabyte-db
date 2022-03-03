package org.yb.client;

import org.yb.master.MasterReplicationOuterClass;

import java.util.List;

public class GetDBStreamInfoResponse extends YRpcResponse {

    private final List<MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.TableInfo>
            tableInfoList;

    public GetDBStreamInfoResponse(long elapsedMillis, String tsUUID,
                                   List<MasterReplicationOuterClass
                                           .GetCDCDBStreamInfoResponsePB.TableInfo>
                                           tableInfoList) {
        super(elapsedMillis, tsUUID);
        this.tableInfoList = tableInfoList;
    }

    public List<MasterReplicationOuterClass
            .GetCDCDBStreamInfoResponsePB.TableInfo> getTableInfoList() {
        return tableInfoList;
    }
}

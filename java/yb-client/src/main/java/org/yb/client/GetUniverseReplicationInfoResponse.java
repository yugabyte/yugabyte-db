// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import org.yb.CommonTypes;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetUniverseReplicationInfoResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final CommonTypes.XClusterReplicationType replicationType;
  private final List<MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.DbScopedInfoPB>
      dbScopedInfos;
  private final List<MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.TableInfoPB>
      tableInfos;

  /**
   * Constructor with information common to all RPCs.
   *
   * @param elapsedMillis     Time in milliseconds since RPC creation to now.
   * @param tsUUIDMasterTypes A string that contains the UUID of the server that answered the RPC.
   */
  public GetUniverseReplicationInfoResponse(
      long elapsedMillis, String tsUUIDMasterTypes, MasterTypes.MasterErrorPB serverError,
      CommonTypes.XClusterReplicationType replicationType,
      List<MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.DbScopedInfoPB> dbScopedInfos,
      List<MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.TableInfoPB> tableInfos) {
    super(elapsedMillis, tsUUIDMasterTypes);
    this.serverError = serverError;
    this.replicationType = replicationType;
    this.dbScopedInfos = dbScopedInfos;
    this.tableInfos = tableInfos;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }

  public CommonTypes.XClusterReplicationType getReplicationType() {
    return replicationType;
  }

  public List<MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.DbScopedInfoPB>
      getDbScopedInfos() {
    return dbScopedInfos;
  }

  public List<MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.TableInfoPB>
      getTableInfos() {
    return tableInfos;
  }
}

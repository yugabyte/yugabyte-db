// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetXClusterOutboundReplicationGroupInfoResponse extends YRpcResponse {
  private final MasterTypes.MasterErrorPB serverError;
  private final List<MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.NamespaceInfoPB>
      namespaceInfos;

  /**
   * Constructor with information common to all RPCs.
   *
   * @param elapsedMillis Time in milliseconds since RPC creation to now.
   * @param tsUUID        A string that contains the UUID of the server that answered the RPC.
   */
  public GetXClusterOutboundReplicationGroupInfoResponse(
      long elapsedMillis,
      String tsUUID,
      MasterTypes.MasterErrorPB serverError,
      List<MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.NamespaceInfoPB> namespaceInfos) {
    super(elapsedMillis, tsUUID);
    this.serverError = serverError;
    this.namespaceInfos = namespaceInfos;
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

  public List<MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.NamespaceInfoPB>
      getNamespaceInfos() {
    return namespaceInfos;
  }
}

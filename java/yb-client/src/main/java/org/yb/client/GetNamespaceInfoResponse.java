// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetNamespaceInfoResponse extends YRpcResponse {

  private final MasterTypes.NamespaceIdentifierPB namespaceInfo;
  private final MasterTypes.MasterErrorPB serverError;

  GetNamespaceInfoResponse(long ellapsedMillis,
                           String tsUUID,
                           MasterTypes.NamespaceIdentifierPB namespaceInfo,
                           MasterTypes.MasterErrorPB serverError) {
    super(ellapsedMillis, tsUUID);
    this.namespaceInfo = namespaceInfo;
    this.serverError = serverError;
  }

  public MasterTypes.NamespaceIdentifierPB getNamespaceInfo() {
    return namespaceInfo;
  }

  public String getNamespaceId() {
    return namespaceInfo.getId().toStringUtf8();
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
}

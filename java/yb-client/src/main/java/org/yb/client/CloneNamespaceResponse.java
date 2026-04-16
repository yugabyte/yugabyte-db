package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class CloneNamespaceResponse extends YRpcResponse {
    private final MasterTypes.MasterErrorPB serverError;

    private String sourceKeyspaceId;
    private Integer cloneSeqNo;

    CloneNamespaceResponse(
        long ellapsedMillis,
        String uuid,
        MasterTypes.MasterErrorPB serverError,
        String sourceKeyspaceId,
        Integer cloneSeqNo) {
      super(ellapsedMillis, uuid);
      this.serverError = serverError;
      this.sourceKeyspaceId = sourceKeyspaceId;
      this.cloneSeqNo = cloneSeqNo;
    }

    public String getSourceKeyspaceId() {
        return sourceKeyspaceId;
    }

    public Integer getCloneSeqNo() {
        return cloneSeqNo;
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

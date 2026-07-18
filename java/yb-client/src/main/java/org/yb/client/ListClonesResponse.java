package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;
import java.util.List;

@InterfaceAudience.Public
public class ListClonesResponse extends YRpcResponse {
    private List<CloneInfo> cloneInfoList;
    private MasterTypes.MasterErrorPB serverError;

    ListClonesResponse(
        long ellapsedMillis,
        String uuid,
        MasterTypes.MasterErrorPB serverError,
        List<CloneInfo> cloneInfoList) {
      super(ellapsedMillis, uuid);
      this.serverError = serverError;
      this.cloneInfoList = cloneInfoList;
    }

    public List<CloneInfo> getCloneInfoList() {
        return cloneInfoList;
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

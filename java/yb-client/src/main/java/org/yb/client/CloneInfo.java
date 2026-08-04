package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo.SysCloneStatePB.State;

@InterfaceAudience.Public
public class CloneInfo {

    private String targetNamespaceId;
    private Integer cloneRequestSeqNo;
    private String sourceNamespaceId;
    private State state;
    private String abortMessage;

    public CloneInfo(
        String targetNamespaceId,
        String sourceNamespaceId,
        Integer cloneRequestSeqNo,
        State state,
        String abortMessage) {
      this.targetNamespaceId = targetNamespaceId;
      this.cloneRequestSeqNo = cloneRequestSeqNo;
      this.sourceNamespaceId = sourceNamespaceId;
      this.state = state;
      this.abortMessage = abortMessage;
    }

    public String getTargetNamespaceId() {
        return targetNamespaceId;
    }

    public Integer getCloneRequestSeqNo() {
        return cloneRequestSeqNo;
    }

    public String getSourceNamespaceId() {
        return sourceNamespaceId;
    }

    public String getAbortMessage() {
        return abortMessage;
    }

    public State getState() {
        return state;
    }
}

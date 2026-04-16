package org.yb.util;

import static org.yb.util.HybridTimeUtil.HTTimestampToPhysicalAndLogical;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.client.CloneInfo;
import org.yb.master.CatalogEntityInfo.SysCloneStatePB;

@InterfaceAudience.Private
public class CloneUtil {
    public static CloneInfo parseCloneInfoPB(SysCloneStatePB cloneInfoPB) {
        SysCloneStatePB.State state = cloneInfoPB.getAggregateState();
        String abortMessage = cloneInfoPB.getAbortMessage();
        CloneInfo cloneInfo =
            new CloneInfo(
                cloneInfoPB.getTargetNamespaceId(),
                cloneInfoPB.getSourceNamespaceId(),
                cloneInfoPB.getCloneRequestSeqNo(),
                state,
                abortMessage);
        return cloneInfo;
    }
}

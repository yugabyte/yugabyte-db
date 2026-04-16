package com.yugabyte.yw.forms;

import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import lombok.Getter;
import lombok.NoArgsConstructor;
import org.yb.master.MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.NamespaceSafeTimePB;

@ApiModel(description = "Get DR config safetime response")
public class DrConfigSafetimeResp {

  @ApiModelProperty(value = "The list of current safetime for each database")
  public List<NamespaceSafetime> safetimes = new ArrayList<>();

  @Getter
  @NoArgsConstructor
  public static class NamespaceSafetime {
    private String namespaceId;
    private String namespaceName;
    private long safetimeEpochUs;
    private long safetimeLagUs;
    private long safetimeSkewUs;
    private double estimatedDataLossMs;

    public NamespaceSafetime(NamespaceSafeTimePB namespaceSafeTimePB, double estimatedDataLossMs) {
      this.namespaceId = namespaceSafeTimePB.getNamespaceId();
      this.namespaceName = namespaceSafeTimePB.getNamespaceName();
      this.safetimeEpochUs =
          computeSafetimeEpochUsFromSafeTimeHt(namespaceSafeTimePB.getSafeTimeHt());
      this.safetimeLagUs = namespaceSafeTimePB.getSafeTimeLag();
      this.safetimeSkewUs = namespaceSafeTimePB.getSafeTimeSkew();
      this.estimatedDataLossMs = estimatedDataLossMs;
    }

    public static long computeSafetimeEpochUsFromSafeTimeHt(long safeTimeHt) {
      return safeTimeHt >> XClusterConfigTaskBase.LOGICAL_CLOCK_NUM_BITS_IN_HYBRID_CLOCK;
    }
  }
}

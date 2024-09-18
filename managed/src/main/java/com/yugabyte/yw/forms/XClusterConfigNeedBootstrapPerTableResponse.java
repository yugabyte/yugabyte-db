package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModelProperty;
import java.util.HashSet;
import java.util.Set;
import lombok.Getter;

@Getter
public class XClusterConfigNeedBootstrapPerTableResponse {

  @ApiModelProperty(value = "Whether bootstrapping is required for the given table")
  private boolean isBootstrapRequired;

  @ApiModelProperty(value = "Explanation for why bootstrapping is required or not")
  private String description;

  @ApiModelProperty(
      value = "Reasons contributing to the decision of whether bootstrapping is required or not")
  private final Set<XClusterNeedBootstrapReason> reasons;

  public XClusterConfigNeedBootstrapPerTableResponse() {
    this.isBootstrapRequired = false;
    this.description =
        "Table on the source is empty and exists on the target universe and thus does not need"
            + " bootstrapping";
    this.reasons = new HashSet<>();
  }

  public void addReason(XClusterNeedBootstrapReason reason) {
    reasons.add(reason);
    computeFinalIsBootstrapRequired();
  }

  private void computeFinalIsBootstrapRequired() {
    boolean hasMissingOnTarget =
        reasons.contains(XClusterNeedBootstrapReason.TABLE_MISSING_ON_TARGET);
    boolean hasData = reasons.contains(XClusterNeedBootstrapReason.TABLE_HAS_DATA);
    boolean isBidirectional =
        reasons.contains(XClusterNeedBootstrapReason.BIDIRECTIONAL_REPLICATION);
    if (isBidirectional) {
      isBootstrapRequired = false;
      if (hasMissingOnTarget) {
        description =
            "The table does not exist on the target universe and must be created through"
                + " bootstrapping. However, bootstrapping will be skipped because the table, a"
                + " sibling YSQL table, or an YCQL index table of the table is being replicated in"
                + " the reverse direction. Thus, the set up replication will fail and the user has"
                + " to create the same table on the other universe before moving forward.";
      } else if (hasData) {
        description =
            "The table has data and the existing data must be replicated through bootstrapping."
                + " However, bootstrapping will be skipped because the table or a sibling YSQL"
                + " table is being replicated in the reverse direction. The data between the two"
                + " universes might be inconsistent.";
      } else {
        description =
            "Bootstrapping will be skipped because the table or a sibling YSQL table is being"
                + " replicated in the reverse direction.";
      }
    } else if (hasMissingOnTarget && hasData) {
      isBootstrapRequired = true;
      description =
          "The table does not exist on the target universe and must be created through"
              + " bootstrapping, and the table has data that needs to be replicated through"
              + " bootstrapping.";
    } else if (hasMissingOnTarget) {
      isBootstrapRequired = true;
      description =
          "The table does not exist on the target universe and must be created through"
              + " bootstrapping.";
    } else if (hasData) {
      isBootstrapRequired = true;
      description =
          "The table has data and the existing data must be replicated through bootstrapping.";
    }
  }

  public enum XClusterNeedBootstrapReason {
    TABLE_MISSING_ON_TARGET,
    TABLE_HAS_DATA,
    BIDIRECTIONAL_REPLICATION;
  }
}

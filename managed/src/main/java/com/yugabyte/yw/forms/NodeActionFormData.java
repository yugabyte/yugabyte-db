package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.NodeActionType;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
public class NodeActionFormData {

  @ApiModelProperty(value = "Action to perform on the node.")
  @NotNull
  public NodeActionType nodeAction;

  @ApiModelProperty(value = "Should a master auto-restart on a stop node action")
  public static Boolean startMasterOnStopNode = true;

  @ApiModelProperty(value = "Should a master auto-restart on a remove node action")
  public static Boolean startMasterOnRemoveNode = true;

  @ApiModelProperty(value = "Should ignore master unavailability and proceed with the node action")
  private boolean force = false;
}

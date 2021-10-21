package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.NodeActionType;
import javax.validation.constraints.NotNull;

public class NodeActionFormData {

  @NotNull public NodeActionType nodeAction;
}

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.NodeActionType;
import play.data.validation.Constraints;

public class NodeActionFormData {

  @Constraints.Required() public NodeActionType nodeAction;
}

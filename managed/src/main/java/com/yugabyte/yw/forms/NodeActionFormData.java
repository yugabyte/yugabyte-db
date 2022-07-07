package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.NodeActionType;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
public class NodeActionFormData {

  @NotNull private NodeActionType nodeAction;
}

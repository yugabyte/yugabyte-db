package com.yugabyte.yw.forms;

import java.util.UUID;
import play.data.validation.Constraints;
import java.util.List;

public class RotateAccessKeyFormData {
  @Constraints.Required() public String newKeyCode;
  @Constraints.Required() public List<UUID> universeUUIDs;
}

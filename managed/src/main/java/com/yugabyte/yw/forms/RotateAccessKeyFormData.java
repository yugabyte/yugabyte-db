package com.yugabyte.yw.forms;

import java.util.UUID;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import java.util.List;

public class RotateAccessKeyFormData {

  public List<UUID> universeUUIDs;

  @NotNull public boolean rotateAllUniverses;

  @NotNull
  @Size(min = 1)
  public String newKeyCode;
}

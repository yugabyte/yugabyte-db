package com.yugabyte.yw.forms;

import java.util.List;
import java.util.UUID;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class RotateAccessKeyFormData {

  public List<UUID> universeUUIDs;

  @NotNull public boolean rotateAllUniverses;

  @NotNull
  @Size(min = 1)
  public String newKeyCode;
}

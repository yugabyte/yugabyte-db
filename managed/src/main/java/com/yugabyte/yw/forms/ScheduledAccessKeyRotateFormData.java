package com.yugabyte.yw.forms;

import java.util.List;
import java.util.UUID;
import javax.validation.constraints.NotNull;

public class ScheduledAccessKeyRotateFormData {

  public List<UUID> universeUUIDs;
  @NotNull public int schedulingFrequencyDays;
  @NotNull public boolean rotateAllUniverses;
}

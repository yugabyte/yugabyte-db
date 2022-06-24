package com.yugabyte.yw.forms;

import java.util.UUID;
import javax.validation.constraints.NotNull;
import java.util.List;

public class ScheduledAccessKeyRotateFormData {

  public List<UUID> universeUUIDs;
  @NotNull public int schedulingFrequencyDays;
  @NotNull public boolean rotateAllUniverses;
}

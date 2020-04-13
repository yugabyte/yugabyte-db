package com.yugabyte.yw.forms;

import java.util.UUID;

import play.data.validation.Constraints;

public class DiskIncreaseFormData extends UniverseDefinitionTaskParams {

  // The universe that we want to perform a rolling restart on.
  @Constraints.Required()
  public UUID universeUUID;

  // Requested size for the disk.
  @Constraints.Required()
  public int size = 0;
}

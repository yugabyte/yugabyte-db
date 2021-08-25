package com.yugabyte.yw.forms;

import java.util.List;
import java.util.UUID;
import play.data.validation.Constraints;

public class XClusterReplicationFormData extends UniverseTaskParams {
  @Constraints.Required() public UUID sourceUniverseUUID;

  public List<String> sourceTableIds;

  public List<String> bootstrapIds;

  public List<String> sourceTableIdsToAdd;

  public List<String> sourceTableIdsToRemove;

  public List<String> sourceMasterAddressesToChange;
}

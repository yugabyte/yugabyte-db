package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@JsonDeserialize(converter = DiskIncreaseFormData.Converter.class)
public class DiskIncreaseFormData extends UniverseDefinitionTaskParams {

  // The universe that we want to perform a rolling restart on.
  @Constraints.Required() @Getter @Setter private UUID universeUUID;

  // Requested size for the disk.
  @Constraints.Required() public int size = 0;

  public static class Converter extends BaseConverter<DiskIncreaseFormData> {}
}

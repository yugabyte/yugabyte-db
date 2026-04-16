package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.forms.AbstractTaskParams;
import java.util.List;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;

@Data
@EqualsAndHashCode(callSuper = false)
@AllArgsConstructor
@NoArgsConstructor
public class ScheduledAccessKeyRotateParams extends AbstractTaskParams
    implements IProviderTaskParams {
  private UUID customerUUID;
  private UUID providerUUID;
  private List<UUID> universeUUIDs;
  private boolean rotateAllUniverses;
}

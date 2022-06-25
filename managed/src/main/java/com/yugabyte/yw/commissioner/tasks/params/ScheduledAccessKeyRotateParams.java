package com.yugabyte.yw.commissioner.tasks.params;

import java.util.List;
import java.util.UUID;

import com.yugabyte.yw.forms.AbstractTaskParams;

import lombok.AllArgsConstructor;
import lombok.NoArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@AllArgsConstructor
@NoArgsConstructor
public class ScheduledAccessKeyRotateParams extends AbstractTaskParams {
  private UUID customerUUID;
  private UUID providerUUID;
  private List<UUID> universeUUIDs;
  private boolean rotateAllUniverses;
}

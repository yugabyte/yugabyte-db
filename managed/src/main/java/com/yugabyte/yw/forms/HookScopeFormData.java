// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.HookScope;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@Getter
@Setter
public class HookScopeFormData {
  @Constraints.Required() private HookScope.TriggerType triggerType;

  private UUID universeUUID;

  private UUID clusterUUID;

  private UUID providerUUID;

  public void verify(UUID customerUUID) {
    if (universeUUID != null && providerUUID != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "At most one of universe UUID and provider UUID can be provided");
    }

    // You can't do cluster without universe
    if (clusterUUID != null && universeUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cluster UUID can only be provided if universe UUID is provided");
    }
    if (HookScope.getByTriggerScopeId(
            customerUUID, triggerType, universeUUID, providerUUID, clusterUUID)
        != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Hook scope with this scope ID and trigger already exists");
    }
  }
}

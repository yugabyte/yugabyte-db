// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import java.util.Optional;
import java.util.UUID;

/** Cheap revision lookup for conditional GET */
public interface ResourceRevisionProvider {

  /**
   * @param customerUUID owning customer
   * @param resourceUUID resource primary key
   * @return revision if the resource exists and belongs to the customer; empty otherwise
   */
  Optional<ResourceRevision> find(UUID customerUUID, UUID resourceUUID);
}

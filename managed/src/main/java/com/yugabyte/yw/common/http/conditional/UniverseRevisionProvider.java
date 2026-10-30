// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import com.google.inject.Singleton;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;

/**
 * Cheap Universe revision from {@code update_time} without loading or parsing {@code
 * universe_details_json}.
 */
@Singleton
public class UniverseRevisionProvider implements ResourceRevisionProvider {

  public static final String RESOURCE_TYPE = "universe";

  @Override
  public Optional<ResourceRevision> find(UUID customerUUID, UUID resourceUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      return Optional.empty();
    }

    return Universe.findRevision(customer.getId(), resourceUUID)
        .flatMap(
            universe -> {
              Date updateTime = universe.getUpdateTime();

              if (updateTime == null) {
                return Optional.empty();
              }

              return Optional.of(
                  ConditionalGets.fromTimestamp(RESOURCE_TYPE, resourceUUID, updateTime));
            });
  }
}

// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import javax.persistence.Embeddable;
import javax.persistence.Entity;
import java.util.UUID;

@Entity
@Embeddable
public class CustomerInstanceKey {
  public UUID instanceId;
  public UUID customerId;

  public boolean equals(Object object) {
    if(object instanceof CustomerInstanceKey) {
      CustomerInstanceKey key = (CustomerInstanceKey) object;
      if(this.instanceId == key.instanceId
          && this.customerId == key.customerId){
        return true;
      }
    }
    return false;
  }

  public int hashCode() {
    return customerId.hashCode() + instanceId.hashCode();
  }

  public static CustomerInstanceKey create(UUID instanceId, UUID customerId) {
    CustomerInstanceKey key = new CustomerInstanceKey();
    key.instanceId = instanceId;
    key.customerId = customerId;
    return key;
  }
}

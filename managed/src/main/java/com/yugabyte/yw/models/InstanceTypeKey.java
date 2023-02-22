// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.io.Serializable;
import java.util.UUID;
import javax.persistence.Embeddable;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@Embeddable
public class InstanceTypeKey implements Serializable {
  private UUID providerUuid;
  private String instanceTypeCode;

  public static InstanceTypeKey create(String instanceTypeCode, UUID providerUuid) {
    InstanceTypeKey key = new InstanceTypeKey();
    key.providerUuid = providerUuid;
    key.instanceTypeCode = instanceTypeCode;
    return key;
  }
}

// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.io.Serializable;
import java.util.UUID;
import javax.persistence.Embeddable;
import javax.persistence.Entity;
import lombok.Data;

@Entity
@Embeddable
@ApiModel
@Data
public class AccessKeyId implements Serializable {
  @ApiModelProperty private String keyCode;
  @ApiModelProperty private UUID providerUUID;

  public static AccessKeyId create(UUID providerUUID, String key_code) {
    AccessKeyId key = new AccessKeyId();
    key.setKeyCode(key_code);
    key.setProviderUUID(providerUUID);
    return key;
  }
}

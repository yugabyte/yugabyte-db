/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.Model;
import lombok.Data;
import lombok.EqualsAndHashCode;

import javax.persistence.Embeddable;
import java.util.UUID;

@Embeddable
@Data
@EqualsAndHashCode(callSuper = false)
public class AlertDefinitionLabelKey extends Model {
  @JsonIgnore private UUID definitionUUID;
  private String name;
}

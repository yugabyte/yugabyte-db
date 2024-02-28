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
import jakarta.persistence.Embeddable;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Embeddable
@Data
@EqualsAndHashCode(callSuper = false)
public class AlertLabelKey extends Model {
  @JsonIgnore private UUID alertUUID;
  private String name;
}

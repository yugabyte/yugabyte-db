/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models;

import java.util.List;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class QueryDistributionSuggestionResponse {
  String adviceType;
  String description;
  String suggestion;
  List<NodeQueryDistributionDetails> details;
  String startTime;
  String endTime;

  @Value
  @Builder
  public static class NodeQueryDistributionDetails {
    String node;
    Integer numSelect;
    Integer numInsert;
    Integer numUpdate;
    Integer numDelete;
  }
}

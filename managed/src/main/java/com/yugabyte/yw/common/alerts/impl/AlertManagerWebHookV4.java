/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts.impl;

import static com.yugabyte.yw.models.helpers.CommonUtils.getMapCommonElements;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import java.time.ZonedDateTime;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Value;
import lombok.extern.jackson.Jacksonized;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;

@Value
@Jacksonized
@Builder
@JsonIgnoreProperties(ignoreUnknown = true)
public class AlertManagerWebHookV4 {
  String receiver;
  Status status;
  List<Alert> alerts;
  Map<String, String> groupLabels;
  String externalURL;
  String version = "4";

  public Map<String, String> getCommonLabels() {
    if (CollectionUtils.isEmpty(alerts)) {
      return Collections.emptyMap();
    }
    return getMapCommonElements(alerts.stream().map(Alert::getLabels).collect(Collectors.toList()));
  }

  public Map<String, String> getCommonAnnotations() {
    if (CollectionUtils.isEmpty(alerts)) {
      return Collections.emptyMap();
    }
    return getMapCommonElements(
        alerts.stream().map(Alert::getAnnotations).collect(Collectors.toList()));
  }

  public String getGroupKey() {
    if (MapUtils.isEmpty(groupLabels)) {
      return "{}:{}";
    }
    String encodedKey =
        groupLabels
            .entrySet()
            .stream()
            .sorted(Entry.comparingByKey())
            .map(entry -> entry.getKey() + ":\\\"" + entry.getValue() + "\\\"")
            .collect(Collectors.joining(", "));
    return "{}:{" + encodedKey + "}";
  }

  @Value
  @Jacksonized
  @Builder
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class Alert {
    Status status;
    Map<String, String> labels;
    Map<String, String> annotations;

    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssxxx")
    ZonedDateTime startsAt;

    @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ssxxx")
    ZonedDateTime endsAt;

    String generatorURL;
  }

  public enum Status {
    firing,
    resolved
  }
}

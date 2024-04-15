package com.yugabyte.troubleshoot.ts.service.filter;

import java.time.Instant;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class ActiveSessionHistoryFilter {
  UUID universeUuid;
  Set<String> nodeNames;
  Set<Long> queryIds;
  Instant startTimestamp;
  Instant endTimestamp;
}

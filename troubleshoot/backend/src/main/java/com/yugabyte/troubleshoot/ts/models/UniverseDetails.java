/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.troubleshoot.ts.models;

import io.ebean.Model;
import io.ebean.annotation.DbJsonB;
import jakarta.validation.constraints.NotNull;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Data
@Entity
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
public class UniverseDetails extends Model implements ModelWithId<UUID> {
  @NotNull @Id private UUID universeUUID;
  @NotNull private String name;
  @NotNull @DbJsonB private UniverseDefinition universeDetails;

  @Override
  public UUID getId() {
    return universeUUID;
  }

  @Data
  public static class UniverseDefinition {

    private List<Cluster> clusters;
    private String nodePrefix;
    private boolean updateInProgress;
    private boolean universePaused;
    public Set<NodeDetails> nodeDetailsSet;

    @Data
    public static class Cluster {
      private UUID uuid;
      private String clusterType;
      private UserIntent userIntent;
    }

    @Data
    public static class UserIntent {
      private String ybSoftwareVersion;
    }

    @Data
    public static class NodeDetails {
      private int nodeIdx;
      private String nodeName;
      private UUID nodeUuid;
      private UUID azUuid;
      private UUID placementUuid;
      private boolean isMaster;
      private boolean isTserver;
      private boolean isYsqlServer;
      private CloudSpecificInfo cloudInfo;
    }

    @Data
    public static class CloudSpecificInfo {
      private String az;
      private String region;
      private String cloud;
    }
  }
}

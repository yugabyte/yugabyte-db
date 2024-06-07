// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.nodeui;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.net.HostAndPort;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import javax.annotation.Nullable;
import lombok.Data;

@Data
public class DumpEntitiesResponse extends NodeUIResponse {

  @JsonProperty("keyspaces")
  private List<Keyspace> keyspaces;

  @JsonProperty("tables")
  private List<Table> tables;

  @JsonProperty("tablets")
  private List<Tablet> tablets;

  @JsonIgnore
  public Set<String> getTabletsByTserverAddress(HostAndPort hp) {
    Set<String> filteredTabletIds = new HashSet<>();
    for (Tablet tablet : tablets) {
      List<Replica> replicas = tablet.getReplicas();
      if (Objects.isNull(replicas)) {
        continue;
      }
      for (Replica replica : replicas) {
        if (replica.getAddr().equals(hp.toString())) {
          filteredTabletIds.add(tablet.getTabletId());
        }
      }
    }
    return filteredTabletIds;
  }

  @Data
  public static class Keyspace {
    @JsonProperty("keyspace_id")
    private String keyspaceId;

    @JsonProperty("keyspace_name")
    private String keyspaceName;

    @JsonProperty("keyspace_type")
    private String keyspaceType;
  }

  @Data
  public static class Table {
    @JsonProperty("table_id")
    private String tableId;

    @JsonProperty("keyspace_id")
    private String keyspaceId;

    @JsonProperty("table_name")
    private String tableName;

    @JsonProperty("state")
    private String state;
  }

  @Data
  public static class Tablet {
    @JsonProperty("table_id")
    private String tableId;

    @JsonProperty("tablet_id")
    private String tabletId;

    @JsonProperty("state")
    private String state;

    @JsonProperty("replicas")
    @Nullable
    private List<Replica> replicas;
  }

  @Data
  public static class Replica {
    @JsonProperty("type")
    private String type;

    @JsonProperty("server_uuid")
    private String serverUuid;

    @JsonProperty("addr")
    private String addr;
  }
}

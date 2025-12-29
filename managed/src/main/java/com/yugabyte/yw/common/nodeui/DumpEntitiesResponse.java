// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.nodeui;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.net.HostAndPort;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
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
  public Set<String> getTabletIdsByTserverAddress(HostAndPort hp) {
    return getTabletsByTserverAddresses(hp).stream()
        .map(t -> t.tabletId)
        .collect(Collectors.toSet());
  }

  @JsonIgnore
  public Set<Tablet> getTabletsByTserverAddresses(HostAndPort... hosts) {
    Set<String> hostStrs = Arrays.stream(hosts).map(h -> h.toString()).collect(Collectors.toSet());
    Set<Tablet> filteredTablets = new HashSet<>();
    for (Tablet tablet : tablets) {
      List<Replica> replicas = tablet.getReplicas();
      if (Objects.isNull(replicas)) {
        continue;
      }
      for (Replica replica : replicas) {
        if (hostStrs.contains(replica.getAddr())) {
          filteredTablets.add(tablet);
          break;
        }
      }
    }
    return filteredTablets;
  }

  @JsonIgnore
  public Set<Table> getTablesByTserverAddresses(HostAndPort... hp) {
    Map<String, Table> tablesById =
        tables.stream().collect(Collectors.toMap(t -> t.tableId, t -> t));

    return getTabletsByTserverAddresses(hp).stream()
        .map(tab -> tablesById.get(tab.tableId))
        .collect(Collectors.toSet());
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

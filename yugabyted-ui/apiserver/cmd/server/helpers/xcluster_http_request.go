package helpers

import (
  "encoding/json"
  "errors"
  "net/url"
)

type Address struct {
  Host string `json:"host"`
  Port int    `json:"port"`
}

type XClusterConfig struct {
  Version int `json:"version"`
}

type ReplicationStatus struct {
  Statuses []Status `json:"statuses"`
}

type Status struct {
  TableID  string `json:"table_id"`
  StreamID string `json:"stream_id"`
}

type TargetUniverseInfo struct {
  UniverseUUID string `json:"universe_uuid"`
  State        string `json:"state"`
}

type ReplicationMetadata struct {
  NamespaceInfos     []ProducerNamespaceInfo `json:"namespace_infos"`
  TargetUniverseInfo *TargetUniverseInfo     `json:"target_universe_info"`
}

type OutboundReplicationGroup struct {
  ReplicationGroupID string              `json:"replication_group_id"`
  Metadata           ReplicationMetadata `json:"metadata"`
  AutomaticDdlMode   bool                `json:"automatic_ddl_mode"`
}

type ProducerNamespaceInfo struct {
  Key   string                 `json:"key"`
  Value ProducerNamespaceValue `json:"value"`
}

type ProducerNamespaceValue struct {
  State      string     `json:"state"`
  TableInfos []KeyValue `json:"table_infos"`
}

type KeyValue struct {
  Key   string     `json:"key"`
  Value TableValue `json:"value"`
}

type TableValue struct {
  StreamID                 string `json:"stream_id"`
  IsCheckpointing          bool   `json:"is_checkpointing"`
  IsPartOfInitialBootstrap bool   `json:"is_part_of_initial_bootstrap"`
}

type DbScopedInfo struct {
  AutomaticDdlMode bool            `json:"automatic_ddl_mode"`
  NamespaceInfos   []NamespaceInfo `json:"namespace_infos"`
}

type NamespaceInfo struct {
  ConsumerNamespaceID string `json:"consumer_namespace_id"`
  ProducerNamespaceID string `json:"producer_namespace_id"`
}

type ReplicationInfo struct {
  ReplicationGroupID                   string           `json:"replication_group_id"`
  ProducerMasterAddresses              []Address        `json:"producer_master_addresses"`
  Tables                               []string         `json:"tables"`
  State                                string           `json:"state"`
  ValidatedTables                      []ValidatedTable `json:"validated_tables"`
  TableStreams                         []TableStream    `json:"table_streams"`
  Transactional                        bool             `json:"transactional"`
  ValidatedLocalAutoFlagsConfigVersion int              `json:"validated_flags_config_version"`
  DbScopedInfo                         DbScopedInfo     `json:"db_scoped_info"`
}

type ValidatedTable struct {
  Key   string `json:"key"`
  Value string `json:"value"`
}

type TableStream struct {
  Key   string `json:"key"`
  Value string `json:"value"`
}

type XClusterResponse struct {
  XClusterConfig            XClusterConfig             `json:"xcluster_config"`
  OutboundReplicationGroups []OutboundReplicationGroup `json:"outbound_replication_groups"`
  OutboundStreams           []interface{}              `json:"outbound_streams"`
  ReplicationStatus         ReplicationStatus          `json:"replication_status"`
  ReplicationInfos          []ReplicationInfo          `json:"replication_infos"`
  ConsumerRegistry          ConsumerRegistry           `json:"consumer_registry"`
}

type ConsumerRegistry struct {
  ProducerMap []ProducerMapEntry `json:"producer_map"`
}

type ProducerMapEntry struct {
  Key   string        `json:"key"`
  Value ProducerValue `json:"value"`
}

type ProducerValue struct {
  StreamMap                       []StreamMap `json:"stream_map"`
  MasterAddrs                     []Address   `json:"master_addrs"`
  CompatibleAutoFlagConfigVersion int         `json:"compatible_auto_flag_config_version"`
  ValidatedAutoFlagsConfigVersion int         `json:"validated_auto_flags_config_version"`
}

type StreamMap struct {
  Key   string      `json:"key"`
  Value StreamValue `json:"value"`
}

type StreamValue struct {
  ConsumerProducerTabletMap []ConsumerProducerTablet `json:"consumer_producer_tablet_map"`
  ConsumerTableID           string                   `json:"consumer_table_id"`
  ProducerTableID           string                   `json:"producer_table_id"`
  SchemaVersions            map[string]int           `json:"schema_versions"`
}

type ConsumerProducerTablet struct {
  Key   string       `json:"key"`
  Value TabletDetail `json:"value"`
}

type TabletDetail struct {
  Tablets  []string `json:"tablets"`
  StartKey []string `json:"start_key"`
  EndKey   []string `json:"end_key"`
}

type XClusterFuture struct {
  XClusterResponse XClusterResponse
  Error            error
}

func (h *HelperContainer) GetXClusterFuture(nodeHost string, future chan XClusterFuture) {
  xCluster := XClusterFuture{
      XClusterResponse: XClusterResponse{},
      Error:            nil,
  }

  body, err := h.BuildMasterURLsAndAttemptGetRequests(
      "api/v1/xcluster", // path
      url.Values{},       // params
      true,               // expectJson
  )
  if err != nil {
      xCluster.Error = err
      future <- xCluster
      return
  }

  var result map[string]interface{}
  err = json.Unmarshal([]byte(body), &result)
  if err != nil {
      xCluster.Error = err
      future <- xCluster
      return
  }

  if val, ok := result["error"]; ok {
      xCluster.Error = errors.New(val.(string))
      future <- xCluster
      return
  }

  err = json.Unmarshal([]byte(body), &xCluster.XClusterResponse)
  xCluster.Error = err

  future <- xCluster
}

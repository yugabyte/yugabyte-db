package models

type XClusterReplicationGroups struct {

    InboundReplicationGroups []XClusterInboundGroup `json:"inbound_replication_groups"`

    OutboundReplicationGroups []XClusterOutboundGroup `json:"outbound_replication_groups"`
}

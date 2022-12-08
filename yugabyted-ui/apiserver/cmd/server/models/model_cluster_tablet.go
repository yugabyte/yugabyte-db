package models

// ClusterTablet - Model representing a tablet
type ClusterTablet struct {

    Namespace string `json:"namespace"`

    TableName string `json:"table_name"`

    TableUuid string `json:"table_uuid"`

    TabletId string `json:"tablet_id"`

    HasLeader bool `json:"has_leader"`
}

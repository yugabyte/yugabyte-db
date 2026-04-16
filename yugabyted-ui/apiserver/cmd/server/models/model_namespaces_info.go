package models

// NamespacesInfo - A single namespace and its corresponding tables participating in the replication
type NamespacesInfo struct {

    // Name of the namespace in the replication
    Namespace string `json:"namespace"`

    TableInfoList []XClusterTableInfoInbound `json:"table_info_list"`
}

package models

// NetworkAllowListInfo - Allow list info
type NetworkAllowListInfo struct {

	Id string `json:"id"`

	ProjectId string `json:"project_id"`

	ClusterIds []string `json:"cluster_ids"`
}

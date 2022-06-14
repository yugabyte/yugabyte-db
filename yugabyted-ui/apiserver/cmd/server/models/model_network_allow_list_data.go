package models

// NetworkAllowListData - Allow list data
type NetworkAllowListData struct {

	Spec NetworkAllowListSpec `json:"spec"`

	Info NetworkAllowListInfo `json:"info"`
}

package models

// InternalNetworkAllowListData - Allow list data
type InternalNetworkAllowListData struct {

	Spec InternalNetworkAllowListSpec `json:"spec"`

	Info NetworkAllowListInfo `json:"info"`
}

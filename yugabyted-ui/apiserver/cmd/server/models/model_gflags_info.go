package models

type GflagsInfo struct {

    MasterFlags map[string]interface{} `json:"masterFlags"`

    TserverFlags map[string]interface{} `json:"tserverFlags"`
}

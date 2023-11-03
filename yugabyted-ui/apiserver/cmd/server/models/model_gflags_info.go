package models

// GflagsInfo - Gflags Info
type GflagsInfo struct {

    MasterFlags []Gflag `json:"master_flags"`

    TserverFlags []Gflag `json:"tserver_flags"`
}

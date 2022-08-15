package models

// MeteringData - Metering data
type MeteringData struct {

	QueryStartTimestamp string `json:"query_start_timestamp"`

	QueryEndTimestamp string `json:"query_end_timestamp"`

	ClusterRecordList []EntityMeteringData `json:"cluster_record_list"`
}

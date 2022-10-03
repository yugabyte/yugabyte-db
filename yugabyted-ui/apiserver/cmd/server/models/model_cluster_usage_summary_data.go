package models

// ClusterUsageSummaryData - Cluster usage summary data
type ClusterUsageSummaryData struct {

	ClusterId string `json:"cluster_id"`

	ClusterName string `json:"cluster_name"`

	IsApacRegion bool `json:"is_apac_region"`

	Instances []UsageSummaryDetail `json:"instances"`

	DataStorage []UsageSummaryDetail `json:"data_storage"`

	PausedDataStorage []UsageSummaryDetail `json:"paused_data_storage"`

	CloudBackupStorage []UsageSummaryDetail `json:"cloud_backup_storage"`

	PausedBackupStorage []UsageSummaryDetail `json:"paused_backup_storage"`

	DataTransfer []UsageSummaryDetail `json:"data_transfer"`

	TotalAmount float64 `json:"total_amount"`
}

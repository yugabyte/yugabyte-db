package models

type VoyagerMigrationDetailsVoyager struct {

    MachineIp string `json:"machine_ip"`

    Os string `json:"os"`

    AvailDiskBytes string `json:"avail_disk_bytes"`

    ExportDir string `json:"export_dir"`

    ExportedSchemaLocation string `json:"exported_schema_location"`
}

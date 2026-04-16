package models

// VoyagerMigrationsInfo - YB Voyager migrations Info
type VoyagerMigrationsInfo struct {

    Migrations []VoyagerMigrationDetails `json:"migrations"`
}

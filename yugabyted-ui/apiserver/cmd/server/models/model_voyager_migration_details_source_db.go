package models

type VoyagerMigrationDetailsSourceDb struct {

    Ip string `json:"ip"`

    Port string `json:"port"`

    Engine string `json:"engine"`

    Version string `json:"version"`

    Database string `json:"database"`

    Schema string `json:"schema"`
}

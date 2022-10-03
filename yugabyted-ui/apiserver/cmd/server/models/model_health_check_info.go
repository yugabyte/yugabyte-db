package models

type HealthCheckInfo struct {

	BuildInfo BuildInfo `json:"build_info"`

	RuntimeInfo RuntimeInfo `json:"runtime_info"`
}

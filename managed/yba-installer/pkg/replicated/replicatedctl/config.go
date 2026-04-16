package replicatedctl

const defaultReplicatedctlPath string = "/usr/local/bin/replicatedctl"

// Config values for running replicatedctl
type Config struct {
	ReplicatedCtlPath string
}

// Path to replicatedctl. ALlows for having a default path if one is not provided
func (cfg Config) Path() string {
	if cfg.ReplicatedCtlPath == "" {
		return defaultReplicatedctlPath
	}
	return cfg.ReplicatedCtlPath
}

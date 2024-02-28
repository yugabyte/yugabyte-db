/*Package replicatedctl implements a low level wrapper around the replicatedtl command. This will
 *be used to do basic controls of any app running on replicated, such as start, stop, inspect, and
 *retrieve config.
 *
 *This implements the core functionality needed to migrate off of replicated, not to fully manage
 *the replicated native scheduler.
 *
 *replicatedctl is run from a full path, defaulting /usr/local/bin/replicatedctl
 */
package replicatedctl

import (
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

const SecretsDirectory = "/var/lib/replicated/secrets"

// ReplicatedCtl client for using replicatedctl
type ReplicatedCtl struct {
	// Path to the replicatedctl executable
	Path string
}

// New creates a new replicatedctl client
func New(cfg Config) *ReplicatedCtl {
	return &ReplicatedCtl{cfg.Path()}
}

// Run a replicatedctl command.
// Args:
//
//	cmd []string: command path. EX: app status -> []string{"app", "status"}
//	              cmd can included flags. EX: --host unix://replicated-cli.sock
//	                                          --quiet
func (r *ReplicatedCtl) run(commands ...string) ([]byte, error) {
	output := shell.Run(r.Path, commands...)
	if !output.SucceededOrLog() {
		return []byte{}, output.Error
	}
	return output.StdoutBytes(), nil
}

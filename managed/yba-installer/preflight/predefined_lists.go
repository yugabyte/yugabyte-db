package preflight

import "github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight/checks"

var InstallChecks = []PreflightCheck{
	checks.User,
	checks.Cpu,
	checks.Memory,
	checks.Port,
	checks.Python,
	checks.Root,
	checks.Ssd,
}

var InstallChecksWithPostgres = append(InstallChecks, checks.Postgres)

var UpgradeChecks = []PreflightCheck{
	checks.InstallExists,
}

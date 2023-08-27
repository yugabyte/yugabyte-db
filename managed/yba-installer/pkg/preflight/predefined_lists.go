package preflight

import "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight/checks"

// InstallChecks is a base list of checks for install time
var InstallChecks = []Check{
	checks.InstallNotExists,
	checks.ValidateInstallerConfig,
	checks.User,
	checks.Cpu,
	checks.Memory,
	checks.Port,
	checks.Python,
	checks.DiskAvail,
	checks.License,
	checks.DBConfigCheck,
	checks.ValidateLocaleConfig,
}

// InstallChecksWithPostgres adds onto the base list with postgres checks
var InstallChecksWithPostgres = append(InstallChecks, checks.Postgres)

// UpgradeChecks is the base list of checks for upgrade
var UpgradeChecks = []Check{
	checks.InstallExists,
	checks.ValidateInstallerConfig,
	checks.DiskAvail,
	checks.Cpu,
	checks.Memory,
	checks.Python,
}

var ReplicatedMigrateChecks = []Check{
	checks.InstallNotExists,
	checks.Python,
	checks.License,
	checks.ValidateLocaleConfig,
}

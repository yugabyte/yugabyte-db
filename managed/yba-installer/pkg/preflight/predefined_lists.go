package preflight

import "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/preflight/checks"

// InstallChecks is a base list of checks for install time
var InstallChecks = []Check{
	checks.InstallNotExists,
	checks.ValidateInstallerConfig,
	checks.User,
	checks.HomeDirSpace,
	checks.Cpu,
	checks.Memory,
	checks.Port,
	checks.Python,
	checks.OpenSSL,
	checks.DiskAvail,
	checks.License,
	checks.DBConfigCheck,
	checks.ValidateLocaleConfig,
	checks.Prometheus,
	checks.ReplicatedNotExists,
	checks.SystemdUserSupported,
	checks.Glibc,
	checks.Logrotate,
}

// InstallChecksWithPostgres adds onto the base list with postgres checks
var InstallChecksWithPostgres = append(InstallChecks, checks.Postgres)

var InstallPerfAdvisorChecks = []Check{
	checks.InstallNotExists,
	checks.Cpu,
	checks.Memory,
	checks.DiskAvail,
}

// UpgradeChecks is the base list of checks for upgrade
var UpgradeChecks = []Check{
	checks.InstallExists,
	checks.ValidateInstallerConfig,
	checks.HomeDirSpace,
	checks.DiskAvail,
	checks.Cpu,
	checks.Memory,
	checks.Python,
	checks.OpenSSL,
	checks.Prometheus,
	checks.NonRootUpgradeCheck,
	checks.ServicesRunningCheck,
	checks.Glibc,
	checks.Logrotate,
}

var ReplicatedMigrateChecks = []Check{
	checks.InstallNotExists,
	checks.Python,
	checks.OpenSSL,
	checks.License,
	checks.ValidateLocaleConfig,
	checks.MigrateDiskCheck,
	checks.HTTPHACheck,
	checks.Logrotate,
}

package checks

import (
	"fmt"
	"strings"

	"github.com/spf13/viper"
)

// FipsPasswords rejects passwords shorter than the FIPS minimum. It cannot be skipped: the
// validated module refuses them at run time (PBKDF2 for BCFKS keystores and PostgreSQL SCRAM),
// so the install would fail later and less clearly.
var FipsPasswords = &fipsPasswordsCheck{"fips-passwords", false}

// fipsMinPasswordLength is the 112-bit minimum the validated module enforces, in bytes.
const fipsMinPasswordLength = 14

type fipsPasswordsCheck struct {
	name        string
	skipAllowed bool
}

func (f fipsPasswordsCheck) Name() string {
	return f.name
}

func (f fipsPasswordsCheck) SkipAllowed() bool {
	return f.skipAllowed
}

func (f fipsPasswordsCheck) Execute() Result {
	res := Result{Check: f.name, Status: StatusPassed}
	if short := shortFipsPasswords(); len(short) > 0 {
		res.Status = StatusCritical
		res.Error = fmt.Errorf("FIPS mode requires passwords of at least %d characters; too short: "+
			"%s. Set longer values, or leave them empty to have one generated",
			fipsMinPasswordLength, strings.Join(short, ", "))
	}
	return res
}

// shortFipsPasswords lists the configured passwords that are in use and shorter than the FIPS
// minimum. Empty values are not reported: yba-ctl generates a long enough one.
func shortFipsPasswords() []string {
	if !viper.GetBool("fips.enabled") {
		return nil
	}
	passwords := []struct {
		key  string
		used bool
	}{
		{"platform.keyStorePassword", true},
		{"perfAdvisor.tls.keystorePassword", viper.GetBool("perfAdvisor.enabled")},
		{"postgres.install.password", viper.GetBool("postgres.install.enabled")},
		{"postgres.useExisting.password", viper.GetBool("postgres.useExisting.enabled")},
		{"prometheus.authPassword", viper.GetBool("prometheus.enableAuth")},
		{"nodeExporter.authPassword", viper.GetBool("nodeExporter.enableAuth")},
	}
	var short []string
	for _, p := range passwords {
		value := viper.GetString(p.key)
		if p.used && value != "" && len(value) < fipsMinPasswordLength {
			short = append(short, p.key)
		}
	}
	return short
}

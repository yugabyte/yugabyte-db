package testutils

import (
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

// cert.go contains helpers for generating "custom" certs for yba
func CustomCerts(certDirBase string) string {
	// Update the SelfSignedOrg to a more appropriate value, then reset it on exit
	originalSelfSignedOrg := common.SelfSignedOrg
	common.SelfSignedOrg = "Yugabyte Certificate Authority"
	origHost := viper.GetString("host")
	viper.Set("host", "localhost")
	origRoot := viper.GetString("installRoot")
	viper.Set("installRoot", certDirBase)
	defer func() {
		common.SelfSignedOrg = originalSelfSignedOrg
		viper.Set("host", origHost)
		viper.Set("installRoot", origRoot)
	}()

	certDir := common.GetSelfSignedCertsDir()
	// NOTE: The changes above will ensure that yba-installer doesn't treat these as self signed for
	// the tests
	common.GenerateSelfSignedCerts()
	return certDir
}

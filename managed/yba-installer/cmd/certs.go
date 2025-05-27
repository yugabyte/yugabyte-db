package cmd

import (
	"errors"
	"fmt"
	"strings"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var certsCmd = &cobra.Command{
	Use:   "certs",
	Short: "Manage certificates for YugabyteDB Anywhere.",
	Long: "Manage certificates for YugabyteDB Anywhere. Generate new self signed certs, and check " +
		"if the current certs are expired.",
}

var certsGenerateCmd = &cobra.Command{
	Use:   "generate",
	Short: "Generate new self signed certificates for YugabyteDB Anywhere.",
	Long: "Generate new self signed certificates for YugabyteDB Anywhere. This will overwrite " +
		"any existing self signed certificates. It will not overwrite any custom certificates.",
	Run: func(cmd *cobra.Command, args []string) {
		state, err := ybactlstate.LoadState()
		if err != nil {
			log.Fatal(fmt.Sprintf("Failed to load state: %v", err))
		}
		err = precheckGenCerts(state)
		if err != nil {
			log.Fatal(fmt.Sprintf("Precheck failed: %v", err))
		}
		if err := common.GenerateSelfSignedCerts(); err != nil {
			log.Fatal(fmt.Sprintf("Failed to generate self signed certs: %v", err))
		}
		log.Info("Self signed certs generated successfully.")
		if err := createPemFormatKeyAndCert(); err != nil {
			log.Error("unabled to create PEM file, but the root cert and key have been recreated.")
			log.Fatal(fmt.Sprintf("Failed to create PEM format key and cert: %v", err))
		}
		if err := services[YbPlatformServiceName].Restart(); err != nil {
			log.Warn("New certs have been generated, but are unused until the service is restarted " +
				"successfully.")
			log.Fatal(fmt.Sprintf("Failed to restart %s service: %v", YbPlatformServiceName, err))
		}
		if err := common.WaitForYBAReady(ybaCtl.Version()); err != nil {
			log.Warn("New certs have been generated, but are unused until the service is restarted " +
				"successfully.")
			log.Fatal(fmt.Sprintf("Failed to wait for YBA to be ready: %v", err))
		}
		log.Info("YBA is ready to use with the new certs.")
	},
}

func precheckGenCerts(state *ybactlstate.State) error {
	errMsg := make([]string, 0)
	if !state.Config.SelfSignedCert {
		errMsg = append(errMsg, "cannot generate self signed certs when custom certs are used")
	}
	if viper.GetString("server_cert_path") != "" || viper.GetString("server_key_path") != "" {
		errMsg = append(errMsg, "cannot generate self signed certs when server cert or key path is set")
	}
	if len(errMsg) > 0 {
		msg := strings.Join(errMsg, ", ")
		log.Error(msg)
		return errors.New(msg)
	}
	return nil
}

func init() {
	rootCmd.AddCommand(certsCmd)
	certsCmd.AddCommand(certsGenerateCmd)
}

/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eit

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// tlsEncryptionInTransitCmd represents the universe security encryption-in-transit command
var tlsEncryptionInTransitCmd = &cobra.Command{
	Use:     "tls",
	Aliases: []string{"tls-toggle"},
	Short:   "Toggle TLS settings for a universe",
	Long:    "Toggle TLS settings for a universe",
	Example: `yba universe security eit tls --name <universe-name> \
	--client-to-node-encryption <client-to-node-encryption>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to change settings\n",
					formatter.RedColor))
		}

		if cmd.Flags().Changed("upgrade-option") {
			upgradeOption, err := cmd.Flags().GetString("upgrade-option")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if strings.Compare(upgradeOption, "Rolling") == 0 {
				cmd.Help()
				logrus.Fatalln(
					formatter.Colorize(
						"Only a \"Non-Rolling\" type of restart is allowed for TLS upgrade.\n",
						formatter.RedColor,
					),
				)
			}
		}

		clientToNodeEncryption, err := cmd.Flags().GetString("client-to-node-encryption")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(clientToNodeEncryption) {
			clientToNodeEncryption = strings.ToUpper(clientToNodeEncryption)
			if strings.Compare(clientToNodeEncryption, util.EnableOpType) != 0 &&
				strings.Compare(clientToNodeEncryption, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize(
						"Invalid client-to-node-encryption value\n",
						formatter.RedColor,
					),
				)
			}
		}

		nodeToNodeEncryption, err := cmd.Flags().GetString("node-to-node-encryption")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(nodeToNodeEncryption) {
			nodeToNodeEncryption = strings.ToUpper(nodeToNodeEncryption)
			if strings.Compare(nodeToNodeEncryption, util.EnableOpType) != 0 &&
				strings.Compare(nodeToNodeEncryption, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize(
						"Invalid node-to-node-encryption value\n",
						formatter.RedColor,
					),
				)
			}
		}

		// Validations before EIT upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.SecurityOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to change tls configuration for %s: %s",
				util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()

		if len(clusters) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					"No clusters found in universe "+universeName+"\n",
					formatter.RedColor))
		}

		primaryCluster := universeutil.FindClusterByType(clusters, util.PrimaryClusterType)
		if universeutil.IsClusterEmpty(primaryCluster) {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No primary cluster found in universe %s (%s)\n",
						universeName,
						universeUUID,
					),
					formatter.RedColor,
				))
		}
		primaryUserIntent := primaryCluster.GetUserIntent()

		upgradeOption := "Non-Rolling"
		requestBody := ybaclient.TlsToggleParams{
			UpgradeOption: upgradeOption,
			Clusters:      clusters,
		}

		clientToNodeEncryption, err := cmd.Flags().GetString("client-to-node-encryption")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(clientToNodeEncryption) {
			clientToNodeEncryption = strings.ToUpper(clientToNodeEncryption)
			enableClientToNodeEncryption := false
			if strings.Compare(clientToNodeEncryption, util.EnableOpType) == 0 {
				enableClientToNodeEncryption = true
			}

			if enableClientToNodeEncryption == primaryUserIntent.GetEnableClientToNodeEncrypt() {
				logrus.Debugf("Enable client-to-node encryption is already set to %t\n",
					enableClientToNodeEncryption)
			}
			logrus.Debugf(
				"Setting client-to-node encryption to: %t\n",
				enableClientToNodeEncryption,
			)
			requestBody.SetEnableClientToNodeEncrypt(enableClientToNodeEncryption)
		} else {
			logrus.Debugf("Setting client-to-node encryption to: %t\n",
				primaryUserIntent.GetEnableClientToNodeEncrypt())
			requestBody.SetEnableClientToNodeEncrypt(primaryUserIntent.GetEnableClientToNodeEncrypt())
		}

		nodeToNodeEncryption, err := cmd.Flags().GetString("node-to-node-encryption")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(nodeToNodeEncryption) {
			nodeToNodeEncryption = strings.ToUpper(nodeToNodeEncryption)
			enableNodeToNodeEncryption := false
			if strings.Compare(nodeToNodeEncryption, util.EnableOpType) == 0 {
				enableNodeToNodeEncryption = true
			}

			if enableNodeToNodeEncryption == primaryUserIntent.GetEnableNodeToNodeEncrypt() {
				logrus.Debugf("Enable node-to-node encryption is already set to %t\n",
					enableNodeToNodeEncryption)
			}

			logrus.Debugf("Setting node-to-node encryption to: %t\n", enableNodeToNodeEncryption)
			requestBody.SetEnableNodeToNodeEncrypt(enableNodeToNodeEncryption)
		} else {
			logrus.Debugf("Setting node-to-node encryption to: %t\n",
				primaryUserIntent.GetEnableNodeToNodeEncrypt())
			requestBody.SetEnableNodeToNodeEncrypt(primaryUserIntent.GetEnableNodeToNodeEncrypt())
		}

		rootAndClientRootCASame, err := cmd.Flags().GetBool("root-and-client-root-ca-same")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetRootAndClientRootCASame(rootAndClientRootCASame)

		r, response, err := authAPI.UpgradeTLS(universeUUID).TlsToggleParams(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Upgrade TLS")
		}

		logrus.Info(
			fmt.Sprintf("Setting encryption in transit configuration in universe %s (%s)\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, &ybaclient.YBPTask{
			TaskUUID:     util.GetStringPointer(r.GetTaskUUID()),
			ResourceUUID: util.GetStringPointer(universeUUID),
		})
	},
}

func init() {
	tlsEncryptionInTransitCmd.Flags().SortFlags = false

	tlsEncryptionInTransitCmd.Flags().String("client-to-node-encryption", "",
		"[Optional] Client to node encryption. Allowed values: enable, disable.")
	tlsEncryptionInTransitCmd.Flags().String("node-to-node-encryption", "",
		"[Optional] Node to node encryption. Allowed values: enable, disable.")
	tlsEncryptionInTransitCmd.Flags().Bool("root-and-client-root-ca-same", true,
		"[Optional] Use same certificates for node to node and client to node communication.")

}

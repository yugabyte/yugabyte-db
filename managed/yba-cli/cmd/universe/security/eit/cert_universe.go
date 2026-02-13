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

// certEncryptionInTransitCmd represents the universe security encryption-in-transit command
var certEncryptionInTransitCmd = &cobra.Command{
	Use:     "cert",
	Aliases: []string{"cert-rotate", "cert-rotation", "certificates-rotation"},
	Short:   "Rotate certificates for a universe",
	Long: "Rotate certificates for a universe. " +
		"Rotation is supported on all certificate types (except for cert-manager on k8s)",
	Example: `yba universe security eit cert --name <universe-name> --rotate-self-signed-client-cert`,
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
			fmt.Sprintf("Are you sure you want to change certificate configuration for %s: %s",
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

		primaryUserIntent := clusters[0].GetUserIntent()
		enableClientToNodeEncrypt := primaryUserIntent.GetEnableClientToNodeEncrypt()

		upgradeOption, err := cmd.Flags().GetString("upgrade-option")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody := ybaclient.CertsRotateParams{
			UpgradeOption:                  upgradeOption,
			Clusters:                       clusters,
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
			UniverseUUID:                   util.GetStringPointer(universeUUID),
		}

		universeRootCA := universeDetails.GetRootCA()
		universeClientRootCA := universeDetails.GetClientRootCA()

		rootAndClientRootCASame, err := cmd.Flags().GetBool("root-and-client-root-ca-same")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		certs, response, err := authAPI.GetListOfCertificates().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Certificate Rotation - List Certificates")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		rootCAName, err := cmd.Flags().GetString("root-ca")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		rootCAUnchanged := false
		if !util.IsEmptyString(rootCAName) {
			for _, cert := range certs {
				if strings.Compare(cert.GetLabel(), rootCAName) == 0 {
					rootCA := cert.GetUuid()
					if strings.Compare(rootCA, universeRootCA) == 0 {
						// Root CA is same as current - don't fail yet, client root CA might be different
						rootCAUnchanged = true
						logrus.Debugf(
							"Root CA is already set to %s, checking if client root CA is different\n",
							rootCAName,
						)
					} else {
						logrus.Debugf("Setting Root CA to: %s\n", rootCAName)
					}
					requestBody.SetRootCA(cert.GetUuid())
					break
				}
			}
			if requestBody.GetRootCA() == "" {
				logrus.Fatalf(
					formatter.Colorize(
						"No root CA found with name: "+rootCAName+"\n",
						formatter.RedColor,
					),
				)
			}
		} else {
			rootCAUnchanged = true
			logrus.Debugf("Root CA is Universe's Root CA\n")
			requestBody.SetRootCA(universeRootCA)
		}

		clientRootCAName, err := cmd.Flags().GetString("client-root-ca")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		clientRootCAUnchanged := false
		if rootAndClientRootCASame {
			if len(strings.TrimSpace(clientRootCAName)) != 0 &&
				len(strings.TrimSpace(rootCAName)) != 0 &&
				strings.Compare(clientRootCAName, rootCAName) != 0 {
				logrus.Fatalf(
					formatter.Colorize(
						"Set root-and-client-root-ca-same flag only if "+
							"client root CA is same as root CA\n",
						formatter.RedColor))
			} else {
				logrus.Debugf("Root and Client Root CA are same\n")
				requestBody.SetClientRootCA(requestBody.GetRootCA())
				clientRootCAUnchanged = rootCAUnchanged
			}
		} else {
			if !util.IsEmptyString(clientRootCAName) {
				// User explicitly provided a client root CA
				for _, cert := range certs {
					if strings.Compare(cert.GetLabel(), clientRootCAName) == 0 {
						clientRootCA := cert.GetUuid()
						if strings.Compare(clientRootCA, universeClientRootCA) == 0 {
							// Client Root CA is same as current - don't fail yet, root CA might be different
							clientRootCAUnchanged = true
							logrus.Debugf("Client Root CA is already set to %s, checking if root CA is different\n", clientRootCAName)
						} else {
							logrus.Debugf("Setting Client Root CA to: %s\n", clientRootCAName)
						}
						requestBody.SetClientRootCA(cert.GetUuid())
						break
					}
				}
				if requestBody.GetClientRootCA() == "" {
					logrus.Fatalf(
						formatter.Colorize("No client root CA found with name: "+clientRootCAName+"\n",
							formatter.RedColor))
				}
			} else if !enableClientToNodeEncrypt {
				// Client-to-node encryption is disabled, don't set clientRootCA
				// This check must come before checking universeClientRootCA to avoid
				// sending clientRootCA when C2N is disabled (even if universe has one from prior config)
				clientRootCAUnchanged = true
				logrus.Debugf("Client Root CA not set (client-to-node encryption disabled)\n")
			} else if len(universeClientRootCA) != 0 {
				// C2N is enabled and universe has a clientRootCA, use it
				clientRootCAUnchanged = true
				logrus.Debugf("Client Root CA is Universe's Client Root CA\n")
				requestBody.SetClientRootCA(universeClientRootCA)
			} else {
				// C2N is enabled but no clientRootCA set, default to rootCA
				clientRootCAUnchanged = rootCAUnchanged
				logrus.Debugf("Client Root CA defaulting to Root CA (client-to-node encryption enabled)\n")
				requestBody.SetClientRootCA(universeRootCA)
			}
		}

		rotateSelfSignedClientCert, err := cmd.Flags().GetBool("rotate-self-signed-client-cert")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !(universeRootCA == requestBody.GetRootCA() &&
			universeClientRootCA == requestBody.GetClientRootCA()) {
			rotateSelfSignedClientCert = false
		}
		if rotateSelfSignedClientCert {
			logrus.Debug("Rotating self-signed client certificate")
		}
		requestBody.SetSelfSignedClientCertRotate(rotateSelfSignedClientCert)

		rotateSelfSignedServerCert, err := cmd.Flags().GetBool("rotate-self-signed-server-cert")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !(universeRootCA == requestBody.GetRootCA() &&
			universeClientRootCA == requestBody.GetClientRootCA()) {
			rotateSelfSignedServerCert = false
		}
		if rotateSelfSignedServerCert {
			logrus.Debug("Rotating self-signed server certificate")
		}
		requestBody.SetSelfSignedServerCertRotate(rotateSelfSignedServerCert)

		if rotateSelfSignedClientCert || rotateSelfSignedServerCert ||
			(requestBody.GetRootCA() == requestBody.GetClientRootCA()) {
			requestBody.SetRootAndClientRootCASame(true)
			logrus.Debug("Setting rootAndClientRootCASame to: true\n")
		} else {
			requestBody.SetRootAndClientRootCASame(rootAndClientRootCASame)
			logrus.Debugf("Setting rootAndClientRootCASame to: %t\n", rootAndClientRootCASame)
		}

		// Fail if no changes are being made: both CAs unchanged and no self-signed cert rotation
		if rootCAUnchanged && clientRootCAUnchanged &&
			!rotateSelfSignedClientCert && !rotateSelfSignedServerCert {
			logrus.Fatalf(
				formatter.Colorize(
					"No certificate rotation requested: Root CA and Client Root CA are already set "+
						"to the requested values and no self-signed certificate rotation was requested\n",
					formatter.RedColor,
				))
		}

		r, response, err := authAPI.UpgradeCerts(
			universeUUID).CertsRotateParams(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Universe", "Certificate Rotation")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		logrus.Info(
			fmt.Sprintf("Setting encryption in transit configuration in universe %s (%s)\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, ybaclient.YBPTask{
			TaskUUID:     util.GetStringPointer(r.GetTaskUUID()),
			ResourceUUID: util.GetStringPointer(universeUUID),
		})

	},
}

func init() {
	certEncryptionInTransitCmd.Flags().SortFlags = false

	certEncryptionInTransitCmd.Flags().String("root-ca", "", "[Optional] Root Certificate name.")
	certEncryptionInTransitCmd.Flags().String("client-root-ca", "",
		"[Optional] Client Root Certificate name.")
	certEncryptionInTransitCmd.Flags().Bool("rotate-self-signed-client-cert", false,
		"[Optional] Rotates client certificate. "+
			"Cannot rotate client certificate when root CA or client root CA are being rotated "+
			"(default false)")
	certEncryptionInTransitCmd.Flags().Bool("rotate-self-signed-server-cert", false,
		"[Optional] Rotates server certificate. "+
			"Cannot rotate client certificate when root CA or client root CA are being rotated"+
			" (default false)")
	certEncryptionInTransitCmd.Flags().Bool("root-and-client-root-ca-same", true,
		"[Optional] Use same certificates for node to node and client to node communication. "+
			"Set true for rotating server and client certificates "+
			"and when root CA and client root CA are same.")
	certEncryptionInTransitCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	certEncryptionInTransitCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")

}

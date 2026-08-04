/*
 * Copyright (c) YugabyteDB, Inc.
 */

package auth

import (
	"bufio"
	"fmt"
	"net/url"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/session"
)

func authWriteConfigFile(r ybaclient.SessionInfo) {
	err := viper.WriteConfig()
	if err != nil {
		if _, ok := err.(viper.ConfigFileNotFoundError); ok {
			logrus.Info("No config was found a new one will be created.\n")
			//Try to create the file
			err = viper.SafeWriteConfig()
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(
						"Error when writing new config file: "+err.Error()+".\n"+
							"In case of permission errors, please run yba with --config or --directory flag to set the path.\n",
						formatter.RedColor))

			}
		} else {
			logrus.Fatalf(
				formatter.Colorize(
					"Error when writing config file: "+err.Error()+".\n", formatter.RedColor))
		}
	}
	configFileUsed := viper.GetViper().ConfigFileUsed()
	if len(configFileUsed) > 0 {
	} else {
		configFileUsed = "$HOME/.yba-cli/.yba-cli.yaml"
	}
	logrus.Infof(
		formatter.Colorize(
			fmt.Sprintf("Configuration file '%v' successfully updated.\n",
				configFileUsed), formatter.GreenColor))

	sessionCtx := formatter.Context{
		Command: "auth",
		Output:  os.Stdout,
		Format:  session.NewSessionFormat(viper.GetString("output")),
	}

	session.Write(sessionCtx, []ybaclient.SessionInfo{r})
}

// InitializeAuthenticatedSession initializes the auth client
func InitializeAuthenticatedSession(url *url.URL, apiToken string, showToken bool) {
	// this was established using authToken
	authAPI, err := ybaAuthClient.NewAuthAPIClientInitialize(url, apiToken)
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	r, response, err := authAPI.GetSessionInfo().Execute()
	if err != nil {
		logrus.Debugf("Full HTTP response: %v\n", response)
		util.FatalHTTPError(response, err, "Get Session Info", "Read")

	}

	util.CheckAndDereference(r, "An error occurred while getting session info")

	authAPI.IsCLISupported()

	// Fetch Customer UUID
	err = authAPI.GetCustomerUUID()
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	logrus.Debugf("Fetched Customer UUID without errors.\n")

	viper.GetViper().Set("apiToken", apiToken)
	if !showToken {
		apiToken = util.MaskObject(apiToken)
	}
	r.SetApiToken(apiToken)
	viper.GetViper().Set("user-uuid", r.GetUserUUID())

	authWriteConfigFile(*r)
}

// ViperVariablesInAuth sets the viper variables for host, insecure and ca-cert
func ViperVariablesInAuth(cmd *cobra.Command, force bool) *url.URL {
	hostConfig := viper.GetString("host")
	var caCertPath, host string
	var useInsecure bool
	var url *url.URL
	if !force {
		fmt.Printf("Enter Host [%s]: ", hostConfig)
		// Prompt for the host
		reader := bufio.NewReader(os.Stdin)
		input, err := reader.ReadString('\n')
		if err != nil {
			logrus.Fatalln(
				formatter.Colorize("Could not read host: "+err.Error()+"\n",
					formatter.RedColor))
		}
		// If the input is just a newline, use the default value
		if input == "\n" {
			input = hostConfig + "\n"
		}
		host = strings.TrimSpace(input)
		if len(host) == 0 {
			if util.IsEmptyString(hostConfig) {
				logrus.Fatalln(
					formatter.Colorize("Host cannot be empty.\n",
						formatter.RedColor))
			} else {
				host = hostConfig
			}
		}

		url, err = ybaAuthClient.ParseURL(host)
		if err != nil {
			logrus.Fatal(
				formatter.Colorize(
					err.Error()+"\n",
					formatter.RedColor))
		}

		if url.Scheme == util.HTTPSURLScheme {
			fmt.Print("Connect insecurely without verifying TLS certificate? [y/N]: ")

			reader := bufio.NewReader(os.Stdin)
			insecureInput, err := reader.ReadString('\n')
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize(
						"Could not read input: "+err.Error(),
						formatter.RedColor,
					),
				)
			}

			insecureInput = strings.TrimSpace(strings.ToLower(insecureInput))
			useInsecure = (insecureInput == "y" || insecureInput == "yes")

			if !useInsecure {
				fmt.Print("Enter path to CA certificate file: ")
				caInput, err := reader.ReadString('\n')
				if err != nil {
					logrus.Fatalln(
						formatter.Colorize(
							"Could not read CA cert path: "+err.Error(),
							formatter.RedColor,
						),
					)
				}
				caCertPath = strings.TrimSpace(caInput)

				if caCertPath == "" {
					logrus.Fatalln(
						formatter.Colorize(
							"CA certificate path cannot be empty when using secure connection.\n",
							formatter.RedColor,
						),
					)
				}
			}
		}
	} else {
		hostFlag, err := cmd.Flags().GetString("host")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		// If the host is empty
		if strings.Compare(hostFlag, "http://localhost:9000") == 0 {
			if util.IsEmptyString(hostConfig) {
				host = hostFlag
			} else {
				host = hostConfig
			}
		} else {
			host = hostFlag
		}
		url, err = ybaAuthClient.ParseURL(host)
		if err != nil {
			logrus.Fatal(
				formatter.Colorize(
					err.Error()+"\n",
					formatter.RedColor))
		}
		if url.Scheme == util.HTTPSURLScheme {
			useInsecure = false
			if cmd.Flags().Changed("insecure") {
				useInsecure, err = cmd.Flags().GetBool("insecure")
				if err != nil {
					logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			if !useInsecure {
				caCertPath, err = cmd.Flags().GetString("ca-cert")
				if err != nil {
					logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				if caCertPath == "" {
					logrus.Fatalln(
						formatter.Colorize(
							"CA certificate path cannot be empty when using secure connection.\n",
							formatter.RedColor,
						),
					)
				}
			}
		} else {
			if !useInsecure {
				errMessage := "Invalid or missing value provided for 'insecure'. Setting it to 'true'.\n"
				logrus.Error(formatter.Colorize(errMessage, formatter.YellowColor))
			}
			useInsecure = true
		}
	}
	viper.GetViper().Set("host", &host)
	viper.GetViper().Set("insecure", &useInsecure)
	viper.GetViper().Set("ca-cert", &caCertPath)

	return url

}

/*
 * Copyright (c) YugaByte, Inc.
 */

package auth

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/term"
)

// AuthCmd to connect to the YBA Host using API token
var AuthCmd = &cobra.Command{
	Use:   "auth",
	Short: "Authenticate yba cli",
	Long: "Authenticate the yba cli through this command by providing the host and API Token." +
		" If non-interactive mode is set, provide the host and API Token using flags. " +
		"Default for host is \"http://localhost:9000\"",
	Run: func(cmd *cobra.Command, args []string) {
		force, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
		var apiToken string
		var host string
		var data []byte
		hostConfig := viper.GetString("host")
		if !force {
			fmt.Printf("Enter Host [%s]: ", hostConfig)
			// Prompt for the host
			reader := bufio.NewReader(os.Stdin)
			input, err := reader.ReadString('\n')
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read host: "+err.Error(), formatter.RedColor))
			}
			// If the input is just a newline, use the default value
			if input == "\n" {
				input = hostConfig + "\n"
			}
			host = strings.TrimSpace(input)
			if len(host) == 0 {
				if len(strings.TrimSpace(hostConfig)) == 0 {
					logrus.Fatalln(formatter.Colorize("Host cannot be empty.", formatter.RedColor))
				} else {
					host = hostConfig
				}
			}

			// Prompt for the API token
			fmt.Print("Enter API Token: ")
			data, err = term.ReadPassword(int(os.Stdin.Fd()))
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read apiToken: "+err.Error(), formatter.RedColor))
			}
			apiToken = string(data)

		} else {
			hostFlag, err := cmd.Flags().GetString("host")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
			// If the host is empty
			if strings.Compare(hostFlag, "http://localhost:9000") == 0 {
				if len(strings.TrimSpace(hostConfig)) == 0 {
					host = hostFlag
				} else {
					host = hostConfig
				}
			} else {
				host = hostFlag
			}
			apiToken, err = cmd.Flags().GetString("apiToken")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
		}
		// Validate that apiToken is a valid
		if strings.TrimSpace(apiToken) == "" {
			logrus.Fatalln(formatter.Colorize("apiToken cannot be empty.", formatter.RedColor))
		}
		viper.GetViper().Set("host", &host)
		viper.GetViper().Set("apiToken", &apiToken)

		fmt.Println("\n")

		// Before writing the config, validate that the data is correct
		url, err := ybaAuthClient.ParseURL(host)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		authUtil(url, apiToken)
	},
}

func init() {
	AuthCmd.Flags().SortFlags = false
	AuthCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage. "+
			"Provide the host (--host/-H) and API token (--apiToken/-a) using flags")
}

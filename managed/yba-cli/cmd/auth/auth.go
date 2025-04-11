/*
 * Copyright (c) YugaByte, Inc.
 */

package auth

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
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
	Example: "yba auth -f -H <host> -a <api-token>",
	Run: func(cmd *cobra.Command, args []string) {
		force, err := cmd.Flags().GetBool("force")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var apiToken string
		var data []byte
		url := viperVariablesInAuth(cmd, force)
		if !force {

			// Prompt for the API token
			fmt.Print("Enter API Token: ")
			data, err = term.ReadPassword(int(os.Stdin.Fd()))
			if err != nil {
				logrus.Fatalln(
					formatter.Colorize("Could not read apiToken: "+err.Error()+"\n",
						formatter.RedColor))
			}
			apiToken = string(data)

		} else {
			apiToken, err = cmd.Flags().GetString("apiToken")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		// Validate that apiToken is a valid
		if strings.TrimSpace(apiToken) == "" {
			logrus.Fatalln(formatter.Colorize("apiToken cannot be empty.\n",
				formatter.RedColor))
		}

		logrus.Infoln("\n")

		authUtil(url, apiToken)
	},
}

func init() {
	AuthCmd.Flags().SortFlags = false
	AuthCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage. "+
			"Provide the host (--host/-H) and API token (--apiToken/-a) using flags")
}

/*
 * Copyright (c) YugabyteDB, Inc.
 */

package auth

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
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
		url := ViperVariablesInAuth(cmd, force)
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
		if util.IsEmptyString(apiToken) {
			logrus.Fatalln(formatter.Colorize("apiToken cannot be empty.\n",
				formatter.RedColor))
		}

		logrus.Infoln("\n")

		showToken, err := cmd.Flags().GetBool("show-api-token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		InitializeAuthenticatedSession(url, apiToken, showToken)
	},
}

func init() {
	AuthCmd.Flags().SortFlags = false
	AuthCmd.Flags().
		Bool("show-api-token", false, "[Optional] Show the API token after authentication. (default false)")

	AuthCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage. "+
			"Provide the host (--host/-H) and API token (--apiToken/-a) using flags")
}

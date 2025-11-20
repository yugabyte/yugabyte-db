/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteUserCmd represents the user command
var deleteUserCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere user",
	Long:    "Delete a user in YugabyteDB Anywhere",
	Example: `yba user delete --email <email>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(email) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No user email found to delete\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "user", email),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		userListRequest := authAPI.ListUsers()

		r, response, err := userListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Delete - List Users")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No users with email: %s found\n", email),
					formatter.RedColor,
				))
		}

		var userUUID string
		for _, user := range r {
			if strings.EqualFold(user.GetEmail(), email) {
				userUUID = user.GetUuid()
				break
			}
		}

		deleteUserRequest := authAPI.DeleteUser(userUUID)

		rDelete, response, err := deleteUserRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Delete")
		}

		if rDelete.GetSuccess() {
			msg := fmt.Sprintf("The user %s (%s) has been deleted",
				formatter.Colorize(email, formatter.GreenColor), userUUID)

			logrus.Infoln(msg + "\n")
		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while removing user %s (%s)\n",
						email, userUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteUserCmd.Flags().SortFlags = false
	deleteUserCmd.Flags().StringP("email", "e", "",
		"[Required] The email of the user to be deleted.")
	deleteUserCmd.MarkFlagRequired("email")
	deleteUserCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

}

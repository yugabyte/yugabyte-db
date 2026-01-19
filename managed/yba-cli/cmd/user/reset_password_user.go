/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/user"
)

var resetPasswordUserCmd = &cobra.Command{
	Use:     "reset-password",
	Aliases: []string{"reset"},
	Short:   "Reset password of currently logged in YugabyteDB Anywhere user",
	Long:    "Reset password of currently logged in user in YugabyteDB Anywhere",
	Example: `yba user reset-password \
	 --current-password <current-password> --new-password <new-password>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		currentPassword, err := cmd.Flags().GetString("current-password")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		newPassword, err := cmd.Flags().GetString("new-password")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(currentPassword) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No current password found\n",
					formatter.RedColor))

		}
		if util.IsEmptyString(newPassword) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No new password found\n",
					formatter.RedColor))
		}
		if strings.Compare(
			strings.TrimSpace(currentPassword),
			strings.TrimSpace(newPassword),
		) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"New password cannot be same as current password\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		currentPassword, err := cmd.Flags().GetString("current-password")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		newPassword, err := cmd.Flags().GetString("new-password")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.UserPasswordChangeFormData{
			CurrentPassword: util.GetStringPointer(strings.TrimSpace(currentPassword)),
			NewPassword:     util.GetStringPointer(strings.TrimSpace(newPassword)),
		}

		rUpdate, response, err := authAPI.ResetUserPassword().Users(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Reset Password")
		}

		if !rUpdate.GetSuccess() {
			logrus.Fatalf(
				formatter.Colorize(
					"An error occurred while updating password for current user\n",
					formatter.RedColor))
		}

		rGet, response, err := authAPI.GetUserDetails(viper.GetString("user-uuid")).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Reset Password - Get User")
		}

		userDetails := util.CheckAndDereference(
			rGet,
			"An error occurred while resetting password for current user",
		)

		r := make([]ybaclient.UserWithFeatures, 0)
		r = append(r, userDetails)

		fetchRoleBindingsForListing(
			r[0].GetUuid(),
			authAPI,
			"Reset Password",
		)

		userCtx := formatter.Context{
			Command: "reset",
			Output:  os.Stdout,
			Format:  user.NewUserFormat(viper.GetString("output")),
		}
		user.Write(userCtx, r)

	},
}

func init() {
	resetPasswordUserCmd.Flags().SortFlags = false
	resetPasswordUserCmd.Flags().String("current-password", "",
		"[Required] The current password of the user.")
	resetPasswordUserCmd.MarkFlagRequired("current-password")
	resetPasswordUserCmd.Flags().String("new-password", "",
		"[Required] The new password of the user.")
	resetPasswordUserCmd.MarkFlagRequired("new-password")
}

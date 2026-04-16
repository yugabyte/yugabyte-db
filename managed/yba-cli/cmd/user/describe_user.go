/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"fmt"
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

var describeUserCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere user",
	Long:    "Describe a user in YugabyteDB Anywhere",
	Example: `yba user describe --email <user-email>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		emailFlag, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(emailFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No user email found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		userListRequest := authAPI.ListUsers()
		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rUsers, response, err := userListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Describe")
		}

		r := make([]ybaclient.UserWithFeatures, 0)
		for _, user := range rUsers {
			if strings.EqualFold(user.GetEmail(), email) {
				r = append(r, user)
			}
		}

		fetchRoleBindingsForListing(
			r[0].GetUuid(),
			authAPI,
			"Describe",
		)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullUserContext := *user.NewFullUserContext()
			fullUserContext.Output = os.Stdout
			fullUserContext.Format = user.NewFullUserFormat(viper.GetString("output"))
			fullUserContext.SetFullUser(r[0])
			fullUserContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No users with email: %s found\n", email),
					formatter.RedColor,
				))
		}

		userCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  user.NewUserFormat(viper.GetString("output")),
		}
		user.Write(userCtx, r)

	},
}

func init() {
	describeUserCmd.Flags().SortFlags = false
	describeUserCmd.Flags().StringP("email", "e", "",
		"[Required] The email of the user to be described.")
	describeUserCmd.MarkFlagRequired("email")
}

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

var listUserCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere users",
	Long:    "List YugabyteDB Anywhere users",
	Example: `yba user list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		userListRequest := authAPI.ListUsers()

		rUsers, response, err := userListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "List")
		}

		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r := make([]ybaclient.UserWithFeatures, 0)
		if !util.IsEmptyString(email) {
			for _, user := range rUsers {
				if strings.EqualFold(user.GetEmail(), email) {
					r = append(r, user)
				}
			}
		} else {
			r = rUsers
		}

		fetchRoleBindingsForListing("", authAPI, "List")

		userCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  user.NewUserFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No users found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		user.Write(userCtx, r)

	},
}

func init() {
	listUserCmd.Flags().SortFlags = false

	listUserCmd.Flags().StringP("email", "e", "", "[Optional] Email of the user.")
}

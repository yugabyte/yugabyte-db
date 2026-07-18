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

var updateUserCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere user",
	Long:    "Update a user in YugabyteDB Anywhere",
	Example: `yba user update --email <user-email> --timezone "America/Los_Angeles"`,
	PreRun: func(cmd *cobra.Command, args []string) {
		emailFlag, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(emailFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No user email found to update\n", formatter.RedColor))
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
			util.FatalHTTPError(response, err, "User", "Update - List Users")
		}

		r := make([]ybaclient.UserWithFeatures, 0)
		for _, user := range rUsers {
			if strings.EqualFold(user.GetEmail(), email) {
				r = append(r, user)
			}
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No users with email: %s found\n", email),
					formatter.RedColor,
				))
		}

		updateUser := r[0]

		timezone, err := cmd.Flags().GetString("timezone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.UserProfileData{
			Role:     updateUser.GetRole(),
			Timezone: util.GetStringPointer(timezone),
		}

		rUpdate, response, err := authAPI.UpdateUserProfile(updateUser.GetUuid()).
			Users(req).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Update")
		}

		r = make([]ybaclient.UserWithFeatures, 0)
		updatedUser := ybaclient.UserWithFeatures{
			Uuid:               rUpdate.Uuid,
			Email:              rUpdate.GetEmail(),
			AuthTokenIssueDate: rUpdate.AuthTokenIssueDate,
			CreationDate:       rUpdate.CreationDate,
			CustomerUUID:       rUpdate.CustomerUUID,
			GroupMemberships:   rUpdate.GroupMemberships,
			LdapSpecifiedRole:  rUpdate.LdapSpecifiedRole,
			OidcJwtAuthToken:   rUpdate.OidcJwtAuthToken,
			Primary:            rUpdate.GetPrimary(),
			Role:               rUpdate.Role,
			Timezone:           rUpdate.Timezone,
			UserType:           rUpdate.UserType,
		}
		r = append(r, updatedUser)

		fetchRoleBindingsForListing(
			r[0].GetUuid(),
			authAPI,
			"Update",
		)

		userCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  user.NewUserFormat(viper.GetString("output")),
		}
		user.Write(userCtx, r)

	},
}

func init() {
	updateUserCmd.Flags().SortFlags = false
	updateUserCmd.Flags().StringP("email", "e", "",
		"[Required] The email of the user to be updated.")
	updateUserCmd.MarkFlagRequired("email")
	updateUserCmd.Flags().String("timezone", "",
		"[Optional] The timezone of the user to be updated.")
}

/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"fmt"

	"github.com/sirupsen/logrus"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/user"
)

func fetchRoleBindingsForListing(
	userUUID string,
	authAPI *ybaAuthClient.AuthAPIClient,
	operation string,
) {
	var err error
	user.RoleBindings, err = authAPI.ListRoleBindingRest(
		userUUID,
		"User",
		fmt.Sprintf("%s - List Role Bindings", operation),
	)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

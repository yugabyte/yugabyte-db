/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CheckTaskAfterCreation checks if task object is empty, and throw error if it is
func CheckTaskAfterCreation(task *ybaclient.YBPTask) ybaclient.YBPTask {
	return CheckAndDereference(task, "An error occurred while creating the task")
}

/*
 * Copyright (c) YugaByte, Inc.
 */

package selfsigned

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SelfSignedEITCmd represents the eit command
var SelfSignedEITCmd = &cobra.Command{
	Use:     "self-signed",
	GroupID: "type",
	Short: "Manage a YugabyteDB Anywhere Self Signed encryption" +
		" in transit (EIT) certificate configuration",
	Long: "Manage a Self Signed encryption in transit (EIT)" +
		" certificate configuration in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	SelfSignedEITCmd.Flags().SortFlags = false

	// SelfSignedEITCmd.AddCommand(createSelfSignedEITCmd)
	// SelfSignedEITCmd.AddCommand(updateSelfSignedEITCmd)
	SelfSignedEITCmd.AddCommand(listSelfSignedEITCmd)
	SelfSignedEITCmd.AddCommand(describeSelfSignedEITCmd)
	SelfSignedEITCmd.AddCommand(deleteSelfSignedEITCmd)

	SelfSignedEITCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf("[Optional] The name of the configuration for the action. %s",
			formatter.Colorize(
				"Required for create, delete, describe, update.",
				formatter.GreenColor)))
}

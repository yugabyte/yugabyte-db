/*
 * Copyright (c) YugabyteDB, Inc.
 */

package eit

import (
	"github.com/spf13/cobra"
)

// EncryptionInTransitCmd represents the universe security encryption-in-transit command
var EncryptionInTransitCmd = &cobra.Command{
	Use:     "eit",
	Aliases: []string{"encryption-in-transit", "certs"},
	Short:   "Encryption-in-transit settings for a universe",
	Long:    "Encryption-in-transit settings for a universe",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	EncryptionInTransitCmd.Flags().SortFlags = false

	EncryptionInTransitCmd.AddCommand(tlsEncryptionInTransitCmd)
	EncryptionInTransitCmd.AddCommand(certEncryptionInTransitCmd)

	EncryptionInTransitCmd.PersistentFlags().String("upgrade-option", "Rolling",
		"[Optional] Upgrade Options, defaults to Rolling. "+
			"Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime). "+
			"Only a \"Non-Rolling\" type of restart is allowed for TLS upgrade.")

}

/*
 * Copyright (c) YugaByte, Inc.
 */

package tools

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/cobra/doc"
	"github.com/spf13/viper"
	"golang.org/x/exp/slices"
)

var documentationFormat = []string{"markdown", "yaml", "rest"}

// ToolsCmd Differents tools for Yugabyte developer
var ToolsCmd = &cobra.Command{
	Use:   "tools",
	Short: "Tools command",
	Long:  "Differents tools for Yugabyte developer",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

var generateDocsCmd = &cobra.Command{
	Use:   "gen-doc",
	Short: "Generate docs",
	PreRun: func(cmd *cobra.Command, args []string) {
		if !slices.Contains(documentationFormat, viper.GetString("format")) {
			log.Fatalf("Format only accept %s as value", strings.Join(documentationFormat, ","))

		}
	},
	Long: "Generate docs in differents format to stdin",
	Run: func(cmd *cobra.Command, args []string) {
		var err error
		cmd.Root().DisableAutoGenTag = true
		// Remove the docs contains to avoid having old command still
		// been there as this happen by the past with the get command
		//err = RemoveContents("./docs")
		if err != nil {
			log.Fatal(err)
		}
		switch viper.GetString("format") {
		case "markdown":
			err = doc.GenMarkdownTree(cmd.Root(), "./docs")
		case "yaml":
			err = doc.GenYamlTree(cmd.Root(), "./docs")
		case "rest":
			err = doc.GenReSTTree(cmd.Root(), "./docs")
		}
		if err != nil {
			log.Fatal(err)
		}

	},
}

func init() {
	ToolsCmd.Hidden = true
	ToolsCmd.AddCommand(generateDocsCmd)

	generateDocsCmd.Flags().SortFlags = false
	generateDocsCmd.Flags().String("format", "markdown",
		fmt.Sprintf("[Optional] Documentation output format (%s).",
			strings.Join(documentationFormat, ",")))
	viper.BindPFlag("format", generateDocsCmd.Flags().Lookup("format"))
	viper.SetDefault("format", "markdown")

}

// RemoveContents function
func RemoveContents(dir string) error {
	files, err := filepath.Glob(filepath.Join(dir, "*"))
	if err != nil {
		logrus.Errorf("%s\n", err.Error())
		return err
	}
	for _, file := range files {
		err = os.RemoveAll(file)
		if err != nil {
			logrus.Errorf("%s\n", err.Error())
			return err
		}
	}
	return nil
}

// TreeCmd prints all commands in a tree format
var TreeCmd = &cobra.Command{
	Use:   "tree",
	Short: "Visualize the command tree",
	Long:  "Visualize the command structore of the CLI in tree format",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("yba")
		for i, cmd := range cmd.Root().Commands() {
			printCommandTree(cmd, "", i == len(cmd.Root().Commands())-1)
		}
	},
}

// printCommandTree recursively prints the command tree
func printCommandTree(cmd *cobra.Command, prefix string, isLast bool) {
	connector := "|-- "
	if isLast {
		connector = "`-- "
	}

	fmt.Println(prefix + connector + cmd.Use) // Use fmt.Println for cleaner output

	newPrefix := prefix + "|   "
	if isLast {
		newPrefix = prefix + "    "
	}

	// Get subcommands
	subcommands := cmd.Commands()
	for i, child := range subcommands {
		printCommandTree(child, newPrefix, i == len(subcommands)-1)
	}
}

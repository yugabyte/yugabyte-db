package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replflow"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
)

var replicatedctlCmd = &cobra.Command{
	Use:   "replicatedctl",
	Short: "Wrapper around replicatedctl commands",
}

var appInspectCmd = &cobra.Command{
	Use:   "inspect",
	Short: "run replicatedctl app inspect",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		res, err := ctl.AppInspect()
		fmt.Println(err)
		fmt.Println(res)
	},
}

var appStatusCmd = &cobra.Command{
	Use:   "status",
	Short: "run replicatedctl app status",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		res, err := ctl.AppStatus()
		fmt.Println(err)
		fmt.Println(res)
	},
}

var appStopCmd = &cobra.Command{
	Use:   "stop",
	Short: "run replicatedctl app stop",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		err := ctl.AppStop()
		fmt.Println(err)
	},
}

var appStartCmd = &cobra.Command{
	Use:   "start",
	Short: "run replicatedctl app start",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		err := ctl.AppStart()
		fmt.Println(err)
	},
}

var appConfigCmd = &cobra.Command{
	Use:   "config",
	Short: "run replicatedctl app-config export",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		config, err := ctl.AppConfigExport()
		fmt.Println(err)
		for _, c := range config.EntriesAsSlice() {
			fmt.Println(c)
		}
	},
}

var uninstallCmd = &cobra.Command{
	Use:   "uninstall",
	Short: "uninstall replicated",
	Run: func(cmd *cobra.Command, args []string) {
		if err := replflow.Uninstall(); err != nil {
			fmt.Printf("failed to uninstall replicated: %s\n", err.Error())
		}
		fmt.Println("uninstalled replicated")
	},
}

var appConfigViewCmd = &cobra.Command{
	Use:   "cgview",
	Short: "run replicatedctl app-config view",
	Run: func(cmd *cobra.Command, args []string) {
		ctl := replicatedctl.New(replicatedctl.Config{})
		view, err := ctl.AppConfigView()
		if err != nil {
			panic(err)
		}
		// fmt.Println(view)
		if len(args) == 1 {
			v, e := view.Get(args[0])
			fmt.Println(e)
			fmt.Println(v)
		}
		for _, e := range view.ViewEntries {
			for _, i := range e.Items {
				fmt.Println(i.Name)
			}
		}
		view.PrintKeyVals()
	},
}

func init() {
	if os.Getenv("YBA_MODE") == "dev" {
		rootCmd.AddCommand(replicatedctlCmd)
		replicatedctlCmd.AddCommand(appInspectCmd, appStatusCmd, appStopCmd, appStartCmd, appConfigCmd,
			uninstallCmd, appConfigViewCmd)
	}
}

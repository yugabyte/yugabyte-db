package universe

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"

	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybav2client "github.com/yugabyte/platform-go-client/v2"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	rsformatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/runscript"
)

var runScriptCmd = &cobra.Command{
	Use:   "run-script",
	Short: "Execute a script on database nodes in a universe",
	Long: "Execute a bash script on one or more database nodes in a universe. " +
		"The script can be provided inline via --script-content or as a file " +
		"path on the YBA node via --script-file. Results including stdout, stderr, " +
		"and exit codes are returned for each node.",
	Example: `yba universe run-script --name <universe-name> \
  --script-content "df -h && free -m"

yba universe run-script --name <universe-name> \
  --script-content "df -h" --node-names "yb-1-node-n1,yb-1-node-n2"

yba universe run-script --name <universe-name> \
  --script-file /tmp/diagnostics.sh --timeout-secs 120

yba universe run-script --name <universe-name> \
  --script-content "cat /proc/cpuinfo" --masters-only

yba universe run-script --name <universe-name> \
  --script-file /tmp/check.sh --params "arg1,arg2,arg3"`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		_ = util.MustGetFlagString(cmd, "name")

		scriptContent, err := cmd.Flags().GetString("script-content")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		scriptFile, err := cmd.Flags().GetString("script-file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(scriptContent) && util.IsEmptyString(scriptFile) {
			logrus.Fatalf(
				formatter.Colorize(
					"Exactly one of --script-content or --script-file is required\n",
					formatter.RedColor,
				),
			)
		}
		if !util.IsEmptyString(scriptContent) && !util.IsEmptyString(scriptFile) {
			logrus.Fatalf(
				formatter.Colorize(
					"--script-content and --script-file are mutually exclusive\n",
					formatter.RedColor,
				),
			)
		}

		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.RunScriptOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}
		}

		err = util.ConfirmCommand(
			fmt.Sprintf(
				"Are you sure you want to run a script on database nodes of universe %s",
				util.MustGetFlagString(cmd, "name"),
			),
			viper.GetBool("force"),
		)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.RunScriptOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		scriptOptions := ybav2client.NewScriptOptions()

		scriptContent, err := cmd.Flags().GetString("script-content")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(scriptContent) {
			scriptOptions.SetScriptContent(scriptContent)
		}

		scriptFile, err := cmd.Flags().GetString("script-file")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(scriptFile) {
			scriptOptions.SetScriptFile(scriptFile)
		}

		paramsStr, err := cmd.Flags().GetString("params")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(paramsStr) {
			var params []string
			for _, p := range strings.Split(paramsStr, ",") {
				p = strings.TrimSpace(p)
				if !util.IsEmptyString(p) {
					params = append(params, p)
				}
			}
			scriptOptions.SetParams(params)
		}

		timeoutSecs, err := cmd.Flags().GetInt64("timeout-secs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if cmd.Flags().Changed("timeout-secs") {
			scriptOptions.SetTimeoutSecs(timeoutSecs)
		}

		linuxUser, err := cmd.Flags().GetString("linux-user")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(linuxUser) {
			scriptOptions.SetLinuxUser(linuxUser)
		}

		requestBody := *ybav2client.NewRunScriptRequest(*scriptOptions)

		// Node selection
		nodeNamesStr, err := cmd.Flags().GetString("node-names")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		mastersOnly, err := cmd.Flags().GetBool("masters-only")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		tserversOnly, err := cmd.Flags().GetBool("tservers-only")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		hasNodeSelection := !util.IsEmptyString(nodeNamesStr) || mastersOnly || tserversOnly
		if hasNodeSelection {
			nodeSelection := ybav2client.NewNodeSelection()
			if !util.IsEmptyString(nodeNamesStr) {
				var nodeNames []string
				for _, n := range strings.Split(nodeNamesStr, ",") {
					n = strings.TrimSpace(n)
					if !util.IsEmptyString(n) {
						nodeNames = append(nodeNames, n)
					}
				}
				nodeSelection.SetNodeNames(nodeNames)
			}
			if mastersOnly {
				nodeSelection.SetMastersOnly(true)
			}
			if tserversOnly {
				nodeSelection.SetTserversOnly(true)
			}
			requestBody.SetNodes(*nodeSelection)
		}

		resp, httpResponse, err := authAPI.RunScript(universeUUID).
			RunScriptRequest(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(httpResponse, err, "Universe: Run Script", "Run")
		}

		summary := resp.GetSummary()
		if summary.GetAllSucceeded() {
			logrus.Info(
				fmt.Sprintf(
					"Script executed successfully on all %d node(s) in universe %s (%s)\n",
					summary.GetTotalNodes(),
					universeName,
					universeUUID,
				),
			)
		} else {
			logrus.Fatalf(
				fmt.Sprintf(
					"Script execution completed with %d failure(s) on universe %s (%s)\n",
					summary.GetFailedNodes(),
					universeName,
					universeUUID,
				),
			)
		}

		ctx := formatter.Context{
			Command: "run",
			Output:  os.Stdout,
			Format:  rsformatter.NewSummaryFormat(viper.GetString("output")),
		}
		rsformatter.WriteResponse(ctx, resp)
	},
}

func init() {
	runScriptCmd.Flags().SortFlags = false
	runScriptCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to run the script on.")
	runScriptCmd.MarkFlagRequired("name")
	runScriptCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	runScriptCmd.Flags().Bool("skip-validations", false,
		"[Optional] Skip validations before running the script. [default: false]")
	runScriptCmd.Flags().String("script-content", "",
		fmt.Sprintf("[Optional*] Inline script content to execute on nodes. %s",
			formatter.Colorize(
				"Mutually exclusive with --script-file.",
				formatter.YellowColor,
			)))
	runScriptCmd.Flags().String("script-file", "",
		fmt.Sprintf("[Optional*] Path to a script file on the YBA node. %s",
			formatter.Colorize(
				"Mutually exclusive with --script-content.",
				formatter.YellowColor,
			)))
	runScriptCmd.MarkFlagsMutuallyExclusive("script-content", "script-file")

	runScriptCmd.Flags().String("params", "",
		"[Optional] Comma-separated command-line arguments to pass to the script.")
	runScriptCmd.Flags().Int64("timeout-secs", 60,
		"[Optional] Timeout in seconds for script execution per node (default 60, max 3600).")
	runScriptCmd.Flags().String("linux-user", "yugabyte",
		"[Optional] Linux user to run the script as.")

	runScriptCmd.Flags().String("node-names", "",
		"[Optional] Comma-separated list of specific node names to target.")
	runScriptCmd.Flags().Bool("masters-only", false,
		"[Optional] Run the script only on master nodes. (default: false)")
	runScriptCmd.Flags().Bool("tservers-only", false,
		"[Optional] Run the script only on tserver nodes. (default: false)")
}

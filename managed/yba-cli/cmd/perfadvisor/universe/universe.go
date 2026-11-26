/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// UniverseCmd manages which universes Perf Advisor collects, and where their
// data goes.
var UniverseCmd = &cobra.Command{
	Use:     "universe",
	Aliases: []string{"universes"},
	Short:   "Manage YugabyteDB Anywhere Perf Advisor universe registration",
	Long: "Register and unregister universes with the Perf Advisor collector, " +
		"in one of the three collection modes.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

var registerUniverseCmd = &cobra.Command{
	Use:   "register",
	Short: "Register a universe with the YugabyteDB Anywhere Perf Advisor collector",
	Long: "Register a universe with the Perf Advisor collector.\n\n" +
		"BASIC collects and stores locally. ADVANCED also remote-writes metrics " +
		"into YBA's Prometheus. ONLINE forwards everything to the endpoint named " +
		"by --endpoint-name and keeps nothing locally; the endpoint is pushed to " +
		"the collector before the universe is registered, so an unreachable " +
		"destination fails the task.",
	Example: `yba perf-advisor universe register --universe-name <universe> --mode ONLINE \
    --endpoint-name byoc-prod`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeName, _ := cmd.Flags().GetString("universe-name")
		mode, _ := cmd.Flags().GetString("mode")
		endpointName, _ := cmd.Flags().GetString("endpoint-name")

		if mode == "ONLINE" && endpointName == "" {
			logrus.Fatalf("%s", formatter.Colorize(
				"endpoint-name is required when mode is ONLINE\n", formatter.RedColor))
		}
		if mode != "ONLINE" && endpointName != "" {
			logrus.Fatalf("%s", formatter.Colorize(
				"endpoint-name only applies to ONLINE mode, not "+mode+"\n",
				formatter.RedColor))
		}

		universeUUID := universeUUIDFromName(authAPI, universeName)
		collectorUUID := soleCollectorUUID(authAPI)

		request := authAPI.
			RegisterUniverseWithPACollector(universeUUID, collectorUUID).
			Mode(mode)
		if endpointName != "" {
			request = request.PaEndpointUUID(endpointUUIDFromName(authAPI, endpointName))
		}

		task, response, err := request.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Universe", "Register")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}

		waitForRegistrationTask(authAPI, task.GetTaskUUID(),
			fmt.Sprintf("Registering universe %s with Perf Advisor in %s mode",
				universeName, mode))
	},
}

var unregisterUniverseCmd = &cobra.Command{
	Use:     "unregister",
	Aliases: []string{"deregister"},
	Short:   "Unregister a universe from the YugabyteDB Anywhere Perf Advisor collector",
	Long: "Unregister a universe from the Perf Advisor collector. The universe " +
		"itself is untouched; when no other universe still uses its endpoint, " +
		"YBA also removes that endpoint from the collector.",
	Example: `yba perf-advisor universe unregister --universe-name <universe>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, _ := cmd.Flags().GetString("universe-name")
		err := util.ConfirmCommand(
			"Are you sure you want to unregister universe "+universeName+" from Perf Advisor",
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatalf("%s", formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeName, _ := cmd.Flags().GetString("universe-name")
		universeUUID := universeUUIDFromName(authAPI, universeName)

		task, response, err := authAPI.
			UnregisterUniverseFromPACollector(universeUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Universe", "Unregister")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}

		// A universe that was not registered comes back with no task.
		if task.GetTaskUUID() == "" {
			logrus.Infof("Universe %s was not registered with Perf Advisor\n", universeName)
			return
		}
		waitForRegistrationTask(authAPI, task.GetTaskUUID(),
			fmt.Sprintf("Unregistering universe %s from Perf Advisor", universeName))
	},
}

var statusUniverseCmd = &cobra.Command{
	Use:     "status",
	Aliases: []string{"describe", "get"},
	Short:   "Show how a universe is registered with Perf Advisor",
	Long:    "Show the collection mode a universe is registered in, and its destination",
	Example: `yba perf-advisor universe status --universe-name <universe>`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeName, _ := cmd.Flags().GetString("universe-name")
		universeUUID := universeUUIDFromName(authAPI, universeName)

		status, response, err := authAPI.
			CheckUniversePARegistration(universeUUID).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Universe", "Status")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}

		logrus.Infof("Universe %s: mode %s\n", universeName,
			formatter.Colorize(status.GetMode(), formatter.GreenColor))
		if name := status.GetPaEndpointName(); name != "" {
			logrus.Infof("Forwarding to endpoint %s\n",
				formatter.Colorize(name, formatter.GreenColor))
		}
	},
}

func init() {
	UniverseCmd.AddCommand(registerUniverseCmd)
	UniverseCmd.AddCommand(unregisterUniverseCmd)
	UniverseCmd.AddCommand(statusUniverseCmd)

	for _, cmd := range []*cobra.Command{
		registerUniverseCmd, unregisterUniverseCmd, statusUniverseCmd,
	} {
		cmd.Flags().SortFlags = false
		cmd.Flags().String("universe-name", "",
			"[Required] Name of the universe.")
		cmd.MarkFlagRequired("universe-name")
	}
	registerUniverseCmd.Flags().String("mode", "BASIC",
		"[Optional] Collection mode. Allowed values: BASIC, ADVANCED, ONLINE.")
	registerUniverseCmd.Flags().String("endpoint-name", "",
		"[Optional] Perf Advisor endpoint to forward to. Required for ONLINE mode.")
}

func waitForRegistrationTask(
	authAPI *ybaAuthClient.AuthAPIClient, taskUUID, message string,
) {
	logrus.Info(message + "\n")
	if viper.GetBool("wait") {
		if err := authAPI.WaitForTask(taskUUID, message); err != nil {
			logrus.Fatalf("%s", formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	}
	logrus.Infoln(formatter.Colorize("Done\n", formatter.GreenColor))
}

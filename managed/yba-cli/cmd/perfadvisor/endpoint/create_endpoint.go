/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var createEndpointCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere Perf Advisor endpoint",
	Long: "Create an external Perf Advisor destination. YBA probes both " +
		"endpoints from the Perf Advisor collector before storing anything, so " +
		"an unreachable URL or a rejected credential fails here rather than " +
		"showing up later as dropped data.",
	Example: `yba perf-advisor endpoint create --name byoc-prod \
    --collection-endpoint https://byoc.cloud.yugabyte.com \
    --metrics-endpoint https://byoc.cloud.yugabyte.com/api/v1/otlp/metrics \
    --auth-type BASIC --username writer --password s3cret \
    --ybm-account-id <account-uuid> --ybm-project-id <project-uuid>`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		spec := endpointSpecFromFlags(cmd, nil)
		endpoint, response, err := authAPI.CreatePerfAdvisorEndpoint().
			PerfAdvisorEndpointSpec(spec).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Perf Advisor Endpoint", "Create")
			logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
		}

		logrus.Infof("Created Perf Advisor endpoint %s\n",
			formatter.Colorize(spec.Name, formatter.GreenColor))
		writeEndpointDetails(*endpoint)
	},
}

func init() {
	createEndpointCmd.Flags().SortFlags = false
	addEndpointFlags(createEndpointCmd)
	createEndpointCmd.MarkFlagRequired("name")
	createEndpointCmd.MarkFlagRequired("collection-endpoint")
	createEndpointCmd.MarkFlagRequired("metrics-endpoint")
}

// addEndpointFlags declares the flags create and update share. Update reads the
// stored endpoint first and only overrides what was passed, so nothing here is
// required for it.
func addEndpointFlags(cmd *cobra.Command) {
	cmd.Flags().StringP("name", "n", "", "[Required] Name of the Perf Advisor endpoint.")
	cmd.Flags().String("type", "BYOC",
		"[Optional] Endpoint kind, case-insensitive. Allowed values: BYOC.")
	cmd.Flags().String("collection-endpoint", "",
		"[Required] URL of the destination's Collection API.")
	cmd.Flags().String("metrics-endpoint", "",
		"[Required] URL metrics are sent to.")
	cmd.Flags().String("metrics-type", "otlphttp",
		"[Optional] Metrics protocol, case-insensitive. Allowed values: otlphttp, "+
			"remotewrite.")
	cmd.Flags().String("auth-type", "NONE",
		"[Optional] Authentication for both endpoints, case-insensitive. Allowed values: "+
			"NONE, BASIC.")
	cmd.Flags().String("username", "",
		"[Optional] Username for both endpoints. Required for BASIC.")
	cmd.Flags().String("password", "",
		"[Optional] Password for both endpoints. Required for BASIC.")
	cmd.Flags().String("ybm-account-id", "",
		"[Optional] YugabyteDB Managed account ID, sent as the YBM-Account-ID "+
			"header. Required by a BYOC ingest gateway.")
	cmd.Flags().String("ybm-project-id", "",
		"[Optional] YugabyteDB Managed project ID, sent as the YBM-Project-ID header.")
}

// endpointSpecFromFlags builds a spec from the flags. On update, current is the
// stored spec and supplies every value the user did not pass - the API replaces
// the whole record, so omitted flags must not blank fields.
func endpointSpecFromFlags(
	cmd *cobra.Command, current *ybav2client.PerfAdvisorEndpointSpec,
) ybav2client.PerfAdvisorEndpointSpec {
	spec := ybav2client.PerfAdvisorEndpointSpec{}
	if current != nil {
		spec = *current
	}

	util.MaybeSetFlagString(cmd, "name", &spec.Name)
	if cmd.Flags().Changed("type") {
		value, _ := cmd.Flags().GetString("type")
		spec.Type = ybav2client.PerfAdvisorEndpointType(strings.ToUpper(value))
	} else if spec.Type == "" {
		spec.Type = ybav2client.PerfAdvisorEndpointType("BYOC")
	}
	util.MaybeSetFlagString(cmd, "collection-endpoint", &spec.CollectionEndpoint)
	util.MaybeSetFlagString(cmd, "metrics-endpoint", &spec.MetricsEndpoint)
	if cmd.Flags().Changed("metrics-type") || spec.MetricsType == "" {
		value, _ := cmd.Flags().GetString("metrics-type")
		spec.MetricsType = ybav2client.PerfAdvisorEndpointMetricsType(strings.ToLower(value))
	}

	// One credential pair covers both endpoints: a destination that fronts both
	// with different credentials has not come up, and two pairs of flags would
	// be four more ways to get it wrong.
	if cmd.Flags().Changed("auth-type") ||
		cmd.Flags().Changed("username") ||
		cmd.Flags().Changed("password") {
		authType := strings.ToUpper(util.MaybeGetFlagString(cmd, "auth-type"))
		username, _ := cmd.Flags().GetString("username")
		password, _ := cmd.Flags().GetString("password")
		if authType == "BASIC" && (username == "" || password == "") {
			logrus.Fatalf("%s", formatter.Colorize(
				"username and password are required for BASIC authentication\n",
				formatter.RedColor))
		}
		auth := ybav2client.PerfAdvisorEndpointAuth{Type: authType}
		if username != "" {
			auth.Username = util.GetStringPointer(username)
		}
		if password != "" {
			auth.Password = util.GetStringPointer(password)
		}
		spec.CollectionAuth = &auth
		metricsAuth := auth
		spec.MetricsAuth = &metricsAuth
	}

	if cmd.Flags().Changed("ybm-account-id") {
		value, _ := cmd.Flags().GetString("ybm-account-id")
		spec.YbmAccountId = util.GetStringPointer(value)
	}
	if cmd.Flags().Changed("ybm-project-id") {
		value, _ := cmd.Flags().GetString("ybm-project-id")
		spec.YbmProjectId = util.GetStringPointer(value)
	}

	if spec.Name == "" || spec.CollectionEndpoint == "" || spec.MetricsEndpoint == "" {
		logrus.Fatalf("%s", formatter.Colorize(
			"name, collection-endpoint and metrics-endpoint are required\n",
			formatter.RedColor))
	}
	return spec
}

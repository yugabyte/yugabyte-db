package common

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"text/tabwriter"

	"github.com/spf13/viper"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// IsHappyStatus will check if the given Status is "happy" - or the service is up and running.
func IsHappyStatus(status Status) bool {
	switch status.Status {
	case StatusRunning, StatusUserOwned:
		return true
	default:
		return false
	}
}

// PrintStatus will print the service status and other useful data
func PrintStatus(stateStatus string, statuses ...Status) {

	// YBA-CTL Status
	generalStatus(stateStatus)
	fmt.Fprintln(os.Stdout, "\nServices:")
	// Service Status
	statusHeader()
	for _, status := range statuses {
		var port string
		// If user owned, show where the service is running instead of just the port.
		if status.Status == StatusUserOwned {
			port = fmt.Sprintf("%s:%d", status.Hostname, status.Port)
		} else {
			port = strconv.Itoa(status.Port)
		}
		outString := status.Service + " \t" + status.Version + " \t" + port +
			" \t" + status.LogFileLoc + " \t" + status.BinaryLoc + " \t" + string(status.Status) +
			" \t" + status.Since + " \t"
		fmt.Fprintln(StatusOutput, outString)
	}
	StatusOutput.Flush()
}

// StatusOutput is a tabwriter object used for all status output.
// As we are using debug + align right, make sure all tabs (\t) are prefixed with
// an empty string (' ')
var StatusOutput = tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ',
	tabwriter.Debug|tabwriter.AlignRight)

// Status prints out the header information for the main
// status command.
func statusHeader() {
	outString := "Systemd service" + " \t" + "Version" + " \t" + "Port" + " \t" +
		"Log Files" + " \t" + "Binary Directory" + " \t" + "Running Status" + " \t" + "Since" + " \t"
	fmt.Fprintln(StatusOutput, outString)
}

func generalStatus(stateStatus string) {
	outString := "YBA Url" + " \t" + "Install Root" + " \t" + "yba-ctl config" + " \t" +
		"yba-ctl Logs" + " \t" + "YBA Installer State" + " \t"
	hostnames := SplitInput(viper.GetString("host"))
	if hostnames == nil || len(hostnames) == 0 {
		log.Fatal("Could not read host in yba-ctl.yml")
	}
	ybaUrls := []string{}
	for _, host := range hostnames {
		url := "https://" + host
		if viper.GetInt("platform.port") != 443 {
			url += fmt.Sprintf(":%d", viper.GetInt("platform.port"))
		}
		ybaUrls = append(ybaUrls, url)
	}
	ybaUrl := strings.Join(ybaUrls, ", ")
	statusString := ybaUrl + " \t" + GetBaseInstall() + " \t" + InputFile() + " \t" +
		YbactlLogFile() + " \t" + stateStatus + " \t"

	fmt.Fprintln(StatusOutput, outString)
	fmt.Fprintln(StatusOutput, statusString)
	StatusOutput.Flush()
}

type Status struct {
	Service        string
	Version        string
	Port           int
	Hostname       string
	ConfigLoc      string
	ServiceFileLoc string
	Status         StatusType
	LogFileLoc     string
	Since          string
	BinaryLoc      string
}

type StatusType string

const (
	StatusRunning      StatusType = "Running"
	StatusUserOwned    StatusType = "Not managed by yba installer"
	StatusStopped      StatusType = "Stopped"
	StatusNotInstalled StatusType = "Not Installed"
	StatusErrored      StatusType = "Errored"
	StatusUnlicensed   StatusType = "Unlicensed"
)

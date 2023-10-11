package common

import (
	"fmt"
	"os"
	"strconv"
	"text/tabwriter"

	"github.com/spf13/viper"
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
func PrintStatus(statuses ...Status) {

	// YBA-CTL Status
	generalStatus()
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
			" \t" + status.LogFileLoc + " \t" + string(status.Status) + " \t"
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
		"Log File Locations" + " \t" + "Running Status" + " \t"
	fmt.Fprintln(StatusOutput, outString)
}

func generalStatus() {
	outString := "YBA Url" + " \t" + "Install Root" + " \t" + "yba-ctl config" + " \t" +
		"yba-ctl Logs" + " \t"
	ybaUrl := "https://" + viper.GetString("host")
	if viper.GetInt("platform.port") != 443 {
		ybaUrl += fmt.Sprintf(":%d", viper.GetInt("platform.port"))
	}
	statusString := ybaUrl + " \t" + GetBaseInstall() + " \t" + InputFile() + " \t" +
		YbactlLogFile() + " \t"

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

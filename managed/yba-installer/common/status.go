package common

import (
	"fmt"
	"os"
	"strconv"
	"text/tabwriter"
)

func PrintStatus(statuses ...Status) {
	statusHeader()
	for _, status := range statuses {
		outString := status.Service + "\t" + status.Version + "\t" + strconv.Itoa(status.Port) +
			"\t" + status.ConfigLoc + "\t" + status.ServiceFileLoc + "\t" + string(status.Status) + "\t"
		fmt.Fprintln(StatusOutput, outString)
	}
	StatusOutput.Flush()
}

// StatusOutput is a tabwriter object used for all status output.
var StatusOutput = tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ',
	tabwriter.Debug|tabwriter.AlignRight)

// Status prints out the header information for the main
// status command.
func statusHeader() {
	outString := "Name" + "\t" + "Version" + "\t" + "Port" + "\t" +
		"Config File Locations" + "\t" + "Systemd File Locations" +
		"\t" + "Running Status" + "\t"
	fmt.Fprintln(StatusOutput, outString)
}

type Status struct {
	Service        string
	Version        string
	Port           int
	ConfigLoc      string
	ServiceFileLoc string
	Status         StatusType
}

type StatusType string

const (
	StatusRunning      StatusType = "Running"
	StatusStopped      StatusType = "Stopped"
	StatusNotInstalled StatusType = "Not Installed"
	StatusErrored      StatusType = "Errored"
)

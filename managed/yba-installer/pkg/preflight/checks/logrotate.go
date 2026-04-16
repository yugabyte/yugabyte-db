package checks

import (
	"fmt"
	"os/exec"
)

var Logrotate = &logrotateCheck{
	"logrotate",
	true,
}

type logrotateCheck struct {
	name        string
	skipAllowed bool
}

func (l logrotateCheck) Name() string {
	return l.name
}

func (l logrotateCheck) SkipAllowed() bool {
	return l.skipAllowed
}

func (l logrotateCheck) Execute() Result {
	res := Result{
		Check:  l.name,
		Status: StatusPassed,
	}
	_, err := exec.LookPath("logrotate")
	if err != nil {
		res.Status = StatusWarning
		res.Error = fmt.Errorf("logrotate command not found in PATH")
	}
	return res
}

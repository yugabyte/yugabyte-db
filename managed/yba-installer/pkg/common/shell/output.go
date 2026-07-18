package shell

import (
	"bytes"
	"fmt"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Output is a wrapper around the output of a shell command.
type Output struct {
	Command        string
	Args           []string
	ExitCode       int
	Error          error
	stdout, stderr *bytes.Buffer
}

// NewOutput returns a new output struct.
func NewOutput(command string, args []string) *Output {
	return &Output{
		ExitCode: -1,
		Command:  command,
		Args:     args,

		stdout: bytes.NewBuffer([]byte{}),
		stderr: bytes.NewBuffer([]byte{}),
	}
}

// Succeed checks if the command exited with status 0.
func (o *Output) Succeeded() bool {
	return o.Error == nil
}

// StdoutString returns the stdout as a string
func (o *Output) StdoutString() string {
	return o.stdout.String()
}

// StderrString returns the stderr as a string
func (o *Output) StderrString() string {
	return o.stderr.String()
}

// StdoutBytes returns the stdout as a byte array
func (o *Output) StdoutBytes() []byte {
	return o.stdout.Bytes()
}

// StderrBytes returns the stderr as a byte array
func (o *Output) StderrBytes() []byte {
	return o.stderr.Bytes()
}

// SucceededOrLog can be called to log an error message if the command failed.
func (o *Output) SucceededOrLog() bool {
	if o.Succeeded() {
		return true
	}
	logging.Error(fmt.Sprintf("Command '%s' failed with exit code '%d' and stderr '%s'", o.Command,
		o.ExitCode, o.stderr.String()))
	return false
}

// LogDebug will do a debug log directly to the logfile - no stdout.
func (o *Output) LogDebug() {
	logging.DebugLF(fmt.Sprintf("Ran '%s' with args '%s'", o.Command, strings.Join(o.Args, " ")))
	logging.DebugLF(fmt.Sprintf("Exit code: %d", o.ExitCode))
	logging.DebugLF(fmt.Sprintf("Stdout: %s", o.stdout.String()))
	logging.DebugLF(fmt.Sprintf("Stderr: %s", o.stderr.String()))
}

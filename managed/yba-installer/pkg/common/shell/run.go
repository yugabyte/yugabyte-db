package shell

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// Run will execute the named command with given arguments
// Args:
//
//	name: name of command
//	args: List of arguments to pass to the command
//
// Return:
//
//	*Output
func Run(name string, args ...string) *Output {
	output := NewOutput(name, args)
	cmd := exec.Command(name, args...)
	cmd.Stdout = output.stdout
	cmd.Stderr = output.stderr
	output.Error = cmd.Run()
	output.ExitCode = cmd.ProcessState.ExitCode()
	output.LogDebug()
	return output
}

func RunWithEnvVars(name string, envVars map[string]string, args ...string) *Output {
	output := NewOutput(name, args)
	cmd := exec.Command(name, args...)
	cmd.Stdout = output.stdout
	cmd.Stderr = output.stderr
	cmd.Env = os.Environ()
	for key, value := range envVars {
		env := fmt.Sprintf("%s=%s", key, value)
		cmd.Env = append(cmd.Env, env)
	}
	output.Error = cmd.Run()
	output.ExitCode = cmd.ProcessState.ExitCode()
	output.LogDebug()
	return output
}

// RunWithEnvVarsProgress is RunWithEnvVars, except that output lines carrying progressPrefix are
// logged as they arrive rather than only once the command exits. Everything else is buffered as
// before. Use it where a command can stall for minutes and says why on stdout:
// yb_platform_backup.sh waiting on another backup's lock names the holder every 30s, and buffered
// that reads as a hang.
func RunWithEnvVarsProgress(name string, envVars map[string]string, progressPrefix string,
	args ...string) *Output {
	output := NewOutput(name, args)
	// One per stream: they are written by different pipes, and a shared line buffer would
	// interleave their partial lines.
	stdoutProgress := &progressLogger{prefix: progressPrefix}
	stderrProgress := &progressLogger{prefix: progressPrefix}
	cmd := exec.Command(name, args...)
	cmd.Stdout = io.MultiWriter(output.stdout, stdoutProgress)
	cmd.Stderr = io.MultiWriter(output.stderr, stderrProgress)
	cmd.Env = os.Environ()
	for key, value := range envVars {
		cmd.Env = append(cmd.Env, fmt.Sprintf("%s=%s", key, value))
	}
	output.Error = cmd.Run()
	output.ExitCode = cmd.ProcessState.ExitCode()
	output.LogDebug()
	return output
}

// progressLogger logs those of the whole lines written to it that start with prefix, holding back
// a trailing partial line until the rest of it arrives.
type progressLogger struct {
	prefix  string
	pending bytes.Buffer
	// Overridden in tests; nil logs through logging.Info.
	log func(string)
}

func (l *progressLogger) Write(p []byte) (int, error) {
	l.pending.Write(p)
	for {
		line, err := l.pending.ReadString('\n')
		if err != nil {
			// No newline yet: put the partial line back and wait for the rest.
			l.pending.WriteString(line)
			break
		}
		line = strings.TrimRight(line, "\r\n")
		if strings.HasPrefix(line, l.prefix) {
			message := strings.TrimPrefix(line, l.prefix)
			if l.log != nil {
				l.log(message)
			} else {
				logging.Info(message)
			}
		}
	}
	return len(p), nil
}

// RunShell will run a command in shell mode. Shell mode will allow for pipes, redirects, etc.
// The format of the command will end up as:
//
//	bash -c "name arg1 arg2..argn"
//
// Args:
//
//	name: name of command
//	args: List of arguments to pass to the command
//
// Return:
//
//	*Output
func RunShell(name string, args ...string) *Output {
	return Run("bash", "-c", name+" "+strings.Join(args, " "))
}

// RunAsUser will use 'su <user> -c "command"' to execute the given command and args
// Args:
//
//	user: Username that is running the command
//	name: name of command
//	args: List of arguments to pass to the command
//
// Return:
//
//	*Output
func RunAsUser(user, name string, args ...string) *Output {
	cmdToRun := fmt.Sprintf("%s %s", name, strings.Join(args, " "))
	newArgs := []string{user, "-c", cmdToRun}
	output := NewOutput(name+" as user "+user, args)
	cmd := exec.Command("su", newArgs...)
	cmd.Stdout = output.stdout
	cmd.Stderr = output.stderr
	output.Error = cmd.Run()
	output.ExitCode = cmd.ProcessState.ExitCode()
	output.LogDebug()
	return output
}

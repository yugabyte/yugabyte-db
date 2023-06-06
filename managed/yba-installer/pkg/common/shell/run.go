package shell

import (
	"os/exec"
	"strings"
)

// Run will execute the named command with given arguments
// Args:
//   name: name of command
//   args: List of arguments to pass to the command
// Return:
//   *Output
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

// RunShell will run a command in shell mode. Shell mode will allow for pipes, redirects, etc.
// The format of the command will end up as:
// 		bash -c "name arg1 arg2..argn"
// Args:
//   name: name of command
//   args: List of arguments to pass to the command
// Return:
//   *Output
func RunShell(name string, args ...string) *Output {
	return Run("bash", "-c", name+" "+strings.Join(args, " "))
}

// RunAsUser will use 'sudo -c' to execute the given command and args
// Args:
//   user: Username that is running the command
//   name: name of command
//   args: List of arguments to pass to the command
// Return:
//   *Output
func RunAsUser(user, name string, args ...string) *Output {
	newArgs := append([]string{"-u", user, name}, args...)
	output := NewOutput(name+" as user "+user, args)
	cmd := exec.Command("sudo", newArgs...)
	cmd.Stdout = output.stdout
	cmd.Stderr = output.stderr
	output.Error = cmd.Run()
	output.ExitCode = cmd.ProcessState.ExitCode()
	output.LogDebug()
	return output
}

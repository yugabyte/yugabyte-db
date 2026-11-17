// Copyright (c) YugabyteDB, Inc.

package cdcstate

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// CLIError reports a yb-ts-cli invocation that failed or could not be located.
type CLIError struct {
	Msg string
}

func (e *CLIError) Error() string {
	return e.Msg
}

func cliError(format string, args ...interface{}) error {
	return &CLIError{Msg: fmt.Sprintf(format, args...)}
}

// FindYbTsCli locates the yb-ts-cli binary, preferring binDir when set and otherwise searching
// the standard install layouts on a DB node: the working directory, ~/tserver/bin, ~/master/bin,
// build/latest, then the newest build/* (sorted, so a dev tree resolves deterministically).
func FindYbTsCli(binDir string) (string, error) {
	if binDir != "" {
		p := filepath.Join(binDir, "yb-ts-cli")
		if fileExists(p) {
			return p, nil
		}
		return "", cliError("Error: yb-ts-cli not found in --bin_dir %s", binDir)
	}

	cwd, _ := os.Getwd()
	if p := filepath.Join(cwd, "yb-ts-cli"); fileExists(p) {
		return p, nil
	}

	home, _ := os.UserHomeDir()
	for _, rel := range []string{"tserver/bin", "master/bin"} {
		p := filepath.Join(home, rel, "yb-ts-cli")
		if fileExists(p) {
			return p, nil
		}
	}

	buildRoot := "build"
	if st, err := os.Stat(buildRoot); err == nil && st.IsDir() {
		latest := filepath.Join(buildRoot, "latest")
		if st, err := os.Stat(latest); err == nil {
			resolved := latest
			if st.Mode()&os.ModeSymlink != 0 {
				if target, err := filepath.EvalSymlinks(latest); err == nil {
					resolved = target
				}
			}
			p := filepath.Join(resolved, "bin", "yb-ts-cli")
			if fileExists(p) {
				return p, nil
			}
		}
		entries, err := os.ReadDir(buildRoot)
		if err == nil {
			var dirs []string
			for _, e := range entries {
				if e.IsDir() {
					dirs = append(dirs, e.Name())
				}
			}
			sort.Strings(dirs)
			for _, name := range dirs {
				p := filepath.Join(buildRoot, name, "bin", "yb-ts-cli")
				if fileExists(p) {
					return p, nil
				}
			}
		}
	}

	return "", cliError(
		"Error: Could not find yb-ts-cli.\n" +
			"Place it in the current directory, use --bin_dir, or run from the repo root.",
	)
}

func fileExists(path string) bool {
	st, err := os.Stat(path)
	return err == nil && !st.IsDir()
}

// runCmd runs a command under a timeout and returns its stdout. It buffers stdout whole, so it is
// only for commands whose output is small; runCmdStreaming is for the rest.
func runCmd(cmd []string, timeoutSec int) (string, error) {
	var stdout bytes.Buffer
	err := runCmdStreaming(cmd, timeoutSec, func(r io.Reader) error {
		_, err := io.Copy(&stdout, r)
		return err
	})
	if err != nil {
		return "", err
	}
	return stdout.String(), nil
}

// runCmdStreaming runs a command under a timeout and hands its stdout to consume as it arrives;
// stderr is used only as the error message when the command exits non-zero. consume must read to
// EOF unless it returns an error, because the command cannot finish while its stdout pipe is full.
//
// The timeout matters because yb-ts-cli talks to a tserver that may be unhealthy — precisely the
// case where a support bundle is being collected — and a hung call would stall the whole bundle.
func runCmdStreaming(cmd []string, timeoutSec int, consume func(stdout io.Reader) error) error {
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(timeoutSec)*time.Second)
	defer cancel()

	execCmd := exec.CommandContext(ctx, cmd[0], cmd[1:]...)
	var stderr bytes.Buffer
	execCmd.Stderr = &stderr
	stdout, err := execCmd.StdoutPipe()
	if err != nil {
		return err
	}
	if err := execCmd.Start(); err != nil {
		return cliError("Error running: %s\n%s", strings.Join(cmd, " "), err.Error())
	}
	consumeErr := consume(stdout)
	if consumeErr != nil {
		// Drain, or a command still writing blocks on the full pipe until the timeout kills it.
		_, _ = io.Copy(io.Discard, stdout)
	}
	if err := execCmd.Wait(); err != nil {
		if ctx.Err() == context.DeadlineExceeded {
			return cliError("Error: Command timed out (%ds): %s", timeoutSec, strings.Join(cmd, " "))
		}
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			msg = err.Error()
		}
		return cliError("Error running: %s\n%s", strings.Join(cmd, " "), msg)
	}
	return consumeErr
}

// tsCliBase is yb-ts-cli and the flags every call takes; certsDirName is empty for a plaintext
// tserver.
func tsCliBase(ybTsCli, tserverAddress, certsDirName string) []string {
	cmd := []string{ybTsCli, fmt.Sprintf("--server_address=%s", tserverAddress)}
	if certsDirName != "" {
		cmd = append(cmd, fmt.Sprintf("--certs_dir_name=%s", certsDirName))
	}
	return cmd
}

// ListCdcStateTablets returns the ids of the cdc_state tablets on the given tserver, parsing
// list_tablets output as it streams; parseListTablets explains why it is never held whole.
func ListCdcStateTablets(ybTsCli, tserverAddress, certsDirName string) ([]string, error) {
	cmd := append(tsCliBase(ybTsCli, tserverAddress, certsDirName), "list_tablets")
	var tablets []string
	err := runCmdStreaming(cmd, 60, func(stdout io.Reader) error {
		var err error
		tablets, err = parseListTablets(stdout)
		return err
	})
	if err != nil {
		return nil, err
	}
	return tablets, nil
}

// DumpTabletData has the tserver write one tablet's raw dump to dest, for streamDumpFile to read.
// The path travels in the RPC, so it must be writable by the tserver process. Nothing bounds the
// dump's size — it holds every row of the tablet — but it costs disk, not memory: streamDumpFile
// reads it a line at a time, and it is deleted once its rows are in the CSV.
func DumpTabletData(
	ybTsCli, tserverAddress, certsDirName, tabletID, dest string, timeoutMs int,
) error {
	cmd := append(tsCliBase(ybTsCli, tserverAddress, certsDirName),
		fmt.Sprintf("--timeout_ms=%d", timeoutMs),
		"dump_tablet_data",
		tabletID,
		dest,
	)
	_, err := runCmd(cmd, timeoutMs/1000+30)
	return err
}

package cmd

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/ybactlstate"
)

var journalctlLogDays int

var logGenCmd = &cobra.Command{
	Use:   "log-bundle",
	Short: "Generate a log bundle",
	Long: `Generate a log bundle including yba-installer logs, Yugaware logs, postgres logs, and
other useful data.`,
	Run: func(cmd *cobra.Command, args []string) {
		// Do a quick validation of flags
		if journalctlLogDays < 1 {
			log.Fatal("Invalid journalctl-since-days, must be a positive integer")
		}
		// Create a logbundle directory under ybactl root
		logBundleDir := filepath.Join(common.YbactlInstallDir(), "log_bundles")
		if err := common.MkdirAll(logBundleDir, os.ModePerm); err != nil {
			logging.Fatal(fmt.Sprintf("Failed to created %s: %s", logBundleDir, err.Error()))
		}

		tarName := filepath.Join(logBundleDir,
			fmt.Sprintf("yba_installer_bundle-%d.tar.gz", time.Now().Unix()))
		file, err := os.Create(tarName)
		if err != nil {
			logging.Fatal(fmt.Sprintf("failed to create file %s: %s", tarName, err.Error()))
		}
		defer file.Close()
		gzipWriter := gzip.NewWriter(file)
		defer gzipWriter.Close()
		writer := tar.NewWriter(gzipWriter)
		defer writer.Close()
		logFilesToCollect := []string{
			// yba-ctl files
			common.YbactlLogFile(),
			filepath.Join(common.YbactlInstallDir(), ybactlstate.StateFileName),
			filepath.Join(common.YbactlInstallDir(), common.VersionMetadataJSON),
			common.LicenseFile(),
			filepath.Join(common.YbactlInstallDir(), common.GoBinaryName),

			// application log and config files.
			filepath.Join(common.GetDataRoot(), "logs/application.log"),
			filepath.Join(common.GetDataRoot(), "logs/audit.log"),
			filepath.Join(common.GetDataRoot(), "logs/postgres.log"),
			filepath.Join(common.GetDataRoot(), "prometheus/prometheus.log"),
			filepath.Join(common.GetActiveSymlink(), "yb-platform/conf/yb-platform.conf"),
			filepath.Join(common.GetActiveSymlink(), "prometheus/conf/prometheus.yml"),

			// System files (logs and services)
			"/var/log/messages",
			"/etc/systemd/service/postgres.service",
			"/etc/systemd/service/yb-platform.service",
			"/etc/systemd/service/prometheus.service",
		}

		logDirsToCollect := []string{
			filepath.Join(common.GetActiveSymlink(), "pgsql/conf"),
		}

		journalctlLogsToCollect := []string{
			"journalctl -u postgres",
			"journalctl -u prometheus",
			"journalctl -u yb-platform",
		}

		for _, lc := range logFilesToCollect {
			if err := addFileToTarWriter(lc, writer); err != nil {
				logging.Fatal("failed to create log bundle: " + err.Error())
			}
		}

		for _, dir := range logDirsToCollect {
			if err := addDirToTarWriter(dir, writer); err != nil {
				logging.Fatal("failed to create log bundle: " + err.Error())
			}
		}

		for _, cmd := range journalctlLogsToCollect {
			if err := addJournalctlToTarWriter(cmd, writer); err != nil {
				logging.Fatal("could not add journalctl '" + cmd + "' to log bundle: " + err.Error())
			}
		}

		// Last, add an os_info.txt file
		var osInfoBuf bytes.Buffer
		osInfoBuf.WriteString("Operating System: " + common.OSName() + "\n")
		osInfoBuf.WriteString("Python Version: " + common.PythonVersion() + "\n")
		osInfoBuf.WriteString("Postgres Version: " + services[PostgresServiceName].Version() + "\n")
		osInfoBuf.WriteString("CPU Count: " + fmt.Sprintf("%d", runtime.NumCPU()) + "\n")
		osInfoBuf.WriteString("Memory: " + common.MemoryHuman())
		osInfo := osInfoBuf.String()
		if err := writer.WriteHeader(
			&tar.Header{
				Name: "os_info.txt",
				Mode: 0600,
				Size: int64(len(osInfo)),
			}); err != nil {
			logging.Fatal(err.Error())
		}
		if _, err := writer.Write([]byte(osInfo)); err != nil {
			logging.Fatal(err.Error())
		}
		logging.Info("created support bundle " + tarName)
	},
}

func addFileToTarWriter(filePath string, w *tar.Writer) error {
	file, err := os.Open(filePath)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			logging.Debug("skipping non-existant file " + filePath)
			return nil
		}
		return fmt.Errorf("Could not open file '%s': %w", filePath, err)
	}
	defer file.Close()

	stat, err := file.Stat()
	if err != nil {
		return fmt.Errorf("Could not get stat for file '%s': %w", filePath, err)
	}

	return tarWriterHelper(w, filepath.Base(filePath), stat.Size(), int64(stat.Mode()),
		stat.ModTime(), file)
}

// Does not recursively look into further directories.
func addDirToTarWriter(directory string, w *tar.Writer) error {
	files, err := os.ReadDir(directory)
	if err != nil {
		return fmt.Errorf("could not read directory %s: %w", directory, err)
	}
	for _, file := range files {
		if file.IsDir() {
			logging.Debug(fmt.Sprintf("skipping directory %s", file.Name()))
		}
		if err := addFileToTarWriter(filepath.Join(directory, file.Name()), w); err != nil {
			return fmt.Errorf("failed to add file %s from directory %s: %w", file.Name(), directory, err)
		}
	}
	return nil
}

func addJournalctlToTarWriter(cmd string, w *tar.Writer) error {
	splitCmd := strings.Split(cmd, " ")
	splitCmd = append(splitCmd, "--since", fmt.Sprintf("%d days ago", journalctlLogDays))
	out := shell.Run(splitCmd[0], splitCmd[1:]...)
	if !out.SucceededOrLog() {
		return fmt.Errorf("could not run journalctl: %w", out.Error)
	}
	data := out.StdoutBytes()
	fName := fmt.Sprintf("journalctl-%s", splitCmd[len(splitCmd)-1])
	return tarWriterHelper(w, fName, int64(len(data)), 0666, time.Now(), bytes.NewBuffer(data))
}

func tarWriterHelper(
	w *tar.Writer, name string, size, mode int64, modTime time.Time, data io.Reader) error {
	header := &tar.Header{
		Name:    name,
		Size:    size,
		Mode:    mode,
		ModTime: modTime,
	}
	if err := w.WriteHeader(header); err != nil {
		return fmt.Errorf("failed to write tar header for %s: %w", name, err)
	}

	written, err := io.Copy(w, data)
	// Only fail if we did not write all the data, it may be possible for the file to grow beyond the
	// given size
	if err != nil && written != size {
		return fmt.Errorf("Could not copy the file '%s' data to the tarball: %w", name, err)
	}

	return nil
}

func init() {
	logGenCmd.Flags().IntVar(&journalctlLogDays, "journalctl-since-days", 1,
		"days back to collect logs from journalctl")
	rootCmd.AddCommand(logGenCmd)
}

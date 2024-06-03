package helpers

import (
    "os"
    "path/filepath"
    "strings"
    "fmt"
)

type LockFileInfoFuture struct {
    YbcTaskID        string
    TserverIP        string
    UserOperation    string
    YbdbAPI          string
    DatabaseKeyspace string
    TaskStartTime    string
    Error            error
}

type LockFileListFuture struct {
    Result  []string
    Error   error
}

type LockFileDetails struct {
    Data  []LockFileInfoFuture
}

func (h *HelperContainer) ListLockFilesFuture(dataDir string, operationType string,
                                                    future chan LockFileListFuture) {
    lockFilesFuture := LockFileListFuture{
        Result:       []string{},
        Error:         nil,
    }
    var lockFileSuffix string
    var lockFileNames []string
    if operationType == "backup" {
        lockFileSuffix = "_backup.lock"
    } else if operationType == "restore" {
        lockFileSuffix = "_restore.lock"
    }
    lockFilesDir := filepath.Join(dataDir, "yb-data", "ybc")
    files, err := os.ReadDir(lockFilesDir)
    // Check if the error is because the directory does not exist.
    if err != nil {
        if os.IsNotExist(err) {
            // The directory does not exist, which is not an error,
            // it means backup operation was not run.
            future <- lockFilesFuture
            return
        }
        // For other types of errors, return an error.
        lockFilesFuture.Error = err
        future <- lockFilesFuture
        return
    }

    for _, file := range files {
        if strings.HasSuffix(file.Name(), lockFileSuffix) {
            lockFilesFuture.Result = append(lockFilesFuture.Result,
                                        filepath.Join(lockFilesDir, file.Name()))
            lockFileNames = append(lockFileNames, file.Name())
        }
    }
    h.logger.Infof("Fetched lock files for %s operation: %s", operationType,
                                       strings.Join(lockFileNames, ", "))
    future <- lockFilesFuture
}

func (h *HelperContainer) GetLockFileInfoFuture(lockFile string, future chan LockFileInfoFuture) {
    var lockFileInfo LockFileInfoFuture
    fileContent, err := os.ReadFile(lockFile)
    if err != nil {
        lockFileInfo.Error = fmt.Errorf("failed to read lock file %s: %v", lockFile, err)
        future <- lockFileInfo
        return
    }
    line := strings.TrimSpace(string(fileContent))
    lockFileComponents := strings.Split(line, ",")
    if len(lockFileComponents) != 6 {
        lockFileInfo.Error = fmt.Errorf("unexpected format in lock file %s", lockFile)
        future <- lockFileInfo
        return
    }
    lockFileInfo = LockFileInfoFuture{
        YbcTaskID:        lockFileComponents[0],
        TserverIP:        lockFileComponents[1],
        UserOperation:    lockFileComponents[2],
        YbdbAPI:          lockFileComponents[3],
        DatabaseKeyspace: lockFileComponents[4],
        TaskStartTime:    lockFileComponents[5],
    }
    future <- lockFileInfo
}

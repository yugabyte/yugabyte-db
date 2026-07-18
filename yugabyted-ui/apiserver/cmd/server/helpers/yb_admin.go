package helpers

import (
    "bytes"
    "os/exec"
)

type YBAdminFuture struct {
    Result string
    Error  error
}

func (h *HelperContainer) ListSnapshotSchedules(masterAddresses string) (string,error) {
    ybAdminFuture := make(chan YBAdminFuture)
    params := []string{"-master_addresses", masterAddresses, "list_snapshot_schedules"}
    go h.RunYBAdminFuture(params, ybAdminFuture)
    ybAdminResult := <-ybAdminFuture
    return ybAdminResult.Result, ybAdminResult.Error
}

func (h *HelperContainer) RunYBAdminFuture(params []string, future chan YBAdminFuture) {
    ybAdminFuture := YBAdminFuture{
        Result: "",
        Error:  nil,
    }
    path, err := h.FindBinaryLocation("yb-admin")
    if err != nil {
        ybAdminFuture.Error = err
        future <- ybAdminFuture
        return
    }
    h.logger.Infof("Executing yb-admin with params: %v", params)
    cmd := exec.Command(path, params...)
    var out bytes.Buffer
    var stderr bytes.Buffer
    cmd.Stdout = &out
    cmd.Stderr = &stderr
    err = cmd.Run()
    if err != nil {
        ybAdminFuture.Error = err
        future <- ybAdminFuture
        return
    }
    ybAdminFuture.Result = out.String()
    future <- ybAdminFuture
}

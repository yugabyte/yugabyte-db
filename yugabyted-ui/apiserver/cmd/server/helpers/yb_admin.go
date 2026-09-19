package helpers

import (
    "bytes"
    "context"
    "os/exec"
    "time"
)

// yb-admin blocks indefinitely when it cannot reach the masters, so bound every
// invocation. On a TLS cluster reached without certificates it never returns.
const ybAdminTimeout = 30 * time.Second

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
    // A cluster with node-to-node encryption rejects the connection unless
    // yb-admin is given the certificate directory.
    if CertsDir != "" {
        params = append([]string{"-certs_dir_name", CertsDir}, params...)
    }
    h.logger.Infof("Executing yb-admin with params: %v", params)
    ctx, cancel := context.WithTimeout(context.Background(), ybAdminTimeout)
    defer cancel()
    cmd := exec.CommandContext(ctx, path, params...)
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

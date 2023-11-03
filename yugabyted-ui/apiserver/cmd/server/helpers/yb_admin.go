package helpers

import (
    "bytes"
    "os/exec"
)

type YBAdminFuture struct {
    Result string
    Error  error
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

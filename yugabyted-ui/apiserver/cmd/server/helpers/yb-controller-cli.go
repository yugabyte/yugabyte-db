package helpers

import (
    "bytes"
    "os/exec"
    "fmt"
)

type YBControllerCLIFuture struct {
    Result string
    Error  error
}

func (h *HelperContainer) TaskProgress(taskID string, tServerIP string) (string, error) {
    ybControllerFuture := make(chan YBControllerCLIFuture)
    params := []string{"task_progress", "--task_id", taskID, "--tserver_ip", tServerIP}
    go h.RunYBControllerCLI(params, ybControllerFuture)
    ybControllerResult := <-ybControllerFuture
    return ybControllerResult.Result, ybControllerResult.Error
}

func (h *HelperContainer) RunYBControllerCLI(params []string, future chan YBControllerCLIFuture) {
    ybcontrollerFuture := YBControllerCLIFuture{
        Result: "",
        Error:  nil,
    }
    path, err := h.FindBinaryLocation("yb-controller-cli")
    if err != nil {
        ybcontrollerFuture.Error = fmt.Errorf("failed to find binary location: %v", err)
        future <- ybcontrollerFuture
        return
    }
    h.logger.Infof("Executing yb-controller-cli with parameters: %v", params)
    cmd := exec.Command(path, params...)
    var out bytes.Buffer
    var stderr bytes.Buffer
    cmd.Stdout = &out
    cmd.Stderr = &stderr
    err = cmd.Run()
    if err != nil {
        ybcontrollerFuture.Error = fmt.Errorf("failed to run yb-controller-cli command: %v", err)
        future <- ybcontrollerFuture
        return
    }
    ybcontrollerFuture.Result = out.String()
    future <- ybcontrollerFuture
}

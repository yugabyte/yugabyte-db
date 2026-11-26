// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/scheduler"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/google/uuid"
	"google.golang.org/protobuf/proto"
)

const (
	nodeHealthScriptDir      = "bin" // Relative path to the YB home directory.
	nodeHealthScript         = "node_health.py"
	nodeHealthSnapshotScript = "node_health_snapshot.py"
)

var (
	// Singleton health check runner, initialized once on first use.
	healthChecker     *healthCheckRunner
	onceHealthChecker = &sync.Once{}
)

// HealthCheckerHandler implements task.AsyncTask.
// It is used to run the health check task on a schedule managed by node agent.
type HealthCheckerHandler struct {
	input    *pb.HealthCheckInput
	username string
}

// NewHealthCheckerHandler returns a new instance of HealthCheckerHandler.
func NewHealthCheckerHandler(input *pb.HealthCheckInput, username string) *HealthCheckerHandler {
	return &HealthCheckerHandler{input: input, username: username}
}

// String implements the AsyncTask method.
func (h *HealthCheckerHandler) String() string {
	return "Health Check Task"
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *HealthCheckerHandler) CurrentTaskStatus() *TaskStatus {
	checker := singletonHealthCheckRunner()
	return &TaskStatus{
		Info:       checker.logOut,
		ExitStatus: &ExitStatus{},
	}
}

// Handle implements the entry method of HealthCheckerHandler from the RPC server.
func (h *HealthCheckerHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	checker := singletonHealthCheckRunner()
	checker.scheduleMutex.Lock()
	defer checker.scheduleMutex.Unlock()
	// Start the health check schedule only if needed.
	err := checker.startScheduleIfNeededBlocked(ctx, h.input, h.username)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in starting health check schedule: %s", err.Error())
		return nil, err
	}
	// Read the last health check output using this request's acceptance floor.
	// Pass ctx so Abort/RPC cancel wakes the wait and releases scheduleMutex.
	output, err := checker.lastOutputBlocked(ctx, h.input)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in getting last health check output: %s", err.Error())
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_HealthCheckOutput{
			HealthCheckOutput: output,
		},
	}, nil
}

// healthCheckRunner runs node_health.py on a schedule and retains the latest result.
type healthCheckRunner struct {
	ctx                    context.Context
	scheduleCancel         context.CancelFunc
	scheduleID             uuid.UUID
	scheduleMutex          *sync.Mutex // Protects schedule start/stop.
	outputAvailableWaiter  *util.CondWaiter
	inputPtr               *atomic.Pointer[healthCheckInput]
	lastOutputPtr          *atomic.Pointer[healthCheckOutput]
	lastClientQueryTimePtr *atomic.Pointer[time.Time]
	logOut                 util.Buffer
}

// healthCheckInput is a wrapper around the health check input.
type healthCheckInput struct {
	pbInput  *pb.HealthCheckInput
	username string
}

// healthCheckOutput is a wrapper around the health check output.
type healthCheckOutput struct {
	pbOutput     *pb.HealthCheckOutput
	idleTimedOut bool // True if the idle timeout was reached.
}

// singletonHealthCheckRunner returns the singleton health check runner.
// If not initialized, it creates a new instance.
func singletonHealthCheckRunner() *healthCheckRunner {
	onceHealthChecker.Do(func() {
		ctx := context.Background()
		healthChecker = &healthCheckRunner{
			ctx:                    ctx,
			scheduleMutex:          &sync.Mutex{},
			outputAvailableWaiter:  util.NewCondWaiter(ctx),
			inputPtr:               new(atomic.Pointer[healthCheckInput]),
			lastOutputPtr:          new(atomic.Pointer[healthCheckOutput]),
			lastClientQueryTimePtr: new(atomic.Pointer[time.Time]),
			logOut:                 util.NewBuffer(module.MaxBufferCapacity),
		}
	})
	return healthChecker
}

// mustGetSnapshotScriptPath returns the path of the snapshot script.
// It panics if the pbInput is not set.
func (h *healthCheckInput) mustGetSnapshotScriptPath() string {
	if h.pbInput == nil {
		panic("pbInput is not set")
	}
	ybHomeDir := h.pbInput.GetYbHomeDir()
	scriptDir := filepath.Join(ybHomeDir, nodeHealthScriptDir)
	return filepath.Join(scriptDir, nodeHealthSnapshotScript)
}

// mustGetScriptPath returns the path of the script.
// It panics if the pbInput is not set.
func (h *healthCheckInput) mustGetScriptPath() string {
	if h.pbInput == nil {
		panic("pbInput is not set")
	}
	ybHomeDir := h.pbInput.GetYbHomeDir()
	scriptDir := filepath.Join(ybHomeDir, nodeHealthScriptDir)
	return filepath.Join(scriptDir, nodeHealthScript)
}

// validateInput checks that required HealthCheckInput fields are set.
func validateInput(input *pb.HealthCheckInput) error {
	if input == nil {
		return errors.New("Input is not set")
	}
	if input.GetGenerationId() == "" {
		return errors.New("Generation ID is not set")
	}
	if input.GetYbHomeDir() == "" {
		return errors.New("YB home directory is not set")
	}
	if input.GetScheduleIntervalSec() <= 0 {
		return errors.New("Schedule interval is not set")
	}
	if input.GetIdleTimeoutSec() <= 0 {
		return errors.New("Idle timeout is not set")
	}
	if input.GetRunTimeoutSec() <= 0 {
		return errors.New("Run timeout is not set")
	}
	if input.GetMinResultEpochSecs() <= 0 {
		return errors.New("Min result epoch is not set")
	}
	return nil
}

func (output *healthCheckOutput) clone() *healthCheckOutput {
	cloned := &healthCheckOutput{idleTimedOut: output.idleTimedOut}
	if output.pbOutput != nil {
		cloned.pbOutput = proto.Clone(output.pbOutput).(*pb.HealthCheckOutput)
	}
	return cloned
}

// startScheduleIfNeededBlocked configures the health check input and (re)schedules
// periodic runs if needed. Caller must hold h.scheduleMutex.
func (h *healthCheckRunner) startScheduleIfNeededBlocked(
	ctx context.Context,
	input *pb.HealthCheckInput,
	username string,
) error {
	if err := validateInput(input); err != nil {
		util.FileLogger().Errorf(ctx, "Invalid health check input: %s", err.Error())
		return err
	}
	// Update the last schedule query time to avoid idle timeout.
	h.storeLastClientQueryTime(time.Now())
	if !h.shouldRescheduleLocked(input) {
		// Refresh only the input so non-generation fields (e.g. master_leader_url) are
		// applied on the next run.
		h.storeInput(input, username)
		util.FileLogger().Infof(h.ctx, "Health check already running with input: %+v", input)
		return nil
	}
	util.FileLogger().Infof(h.ctx, "Stopping existing health check schedule")
	err := h.stopLocked()
	if err != nil {
		return err
	}
	util.FileLogger().Infof(h.ctx, "Updating health check input for new schedule")
	if err := h.updateInputLocked(input, username); err != nil {
		h.stopLocked()
		return err
	}
	util.FileLogger().Infof(h.ctx, "Starting health check with input: %+v", input)
	err = h.scheduleLocked()
	return err
}

// storeInput stores the input value in the atomic pointer.
func (h *healthCheckRunner) storeInput(
	input *pb.HealthCheckInput,
	username string,
) *healthCheckInput {
	checkInput := &healthCheckInput{
		pbInput:  proto.Clone(input).(*pb.HealthCheckInput),
		username: username,
	}
	h.inputPtr.Store(checkInput)
	return checkInput
}

// storeLastClientQueryTime stores the last client query time in the atomic pointer.
func (h *healthCheckRunner) storeLastClientQueryTime(t time.Time) *time.Time {
	h.lastClientQueryTimePtr.Store(&t)
	return &t
}

// checkInput returns the input value.
func (h *healthCheckRunner) checkInput() *healthCheckInput {
	return h.inputPtr.Load()
}

// lastOutput returns the last health check output.
func (h *healthCheckRunner) lastOutput() *healthCheckOutput {
	return h.lastOutputPtr.Load()
}

// lastScheduleQueryTime returns the last schedule query time.
func (h *healthCheckRunner) lastScheduleQueryTime() time.Time {
	v := h.lastClientQueryTimePtr.Load()
	if v == nil {
		return time.Time{}
	}
	return *v
}

// shouldRescheduleLocked returns true if the schedule needs to be rescheduled.
func (h *healthCheckRunner) shouldRescheduleLocked(input *pb.HealthCheckInput) bool {
	checkInput := h.checkInput()
	lastOut := h.lastOutput()
	// Reschedule when generation changes, no schedule is active, or idle
	// timeout woke waiters and the schedule is shutting down.
	return checkInput == nil ||
		checkInput.pbInput.GetGenerationId() != input.GetGenerationId() ||
		!scheduler.GetInstance().IsScheduleActive(h.scheduleID) ||
		(lastOut != nil && lastOut.idleTimedOut)
}

// updateInputLocked updates the input for the new schedule in the mutex held by the caller.
func (h *healthCheckRunner) updateInputLocked(input *pb.HealthCheckInput, username string) error {
	checkInput := h.storeInput(input, username)
	scriptPath := checkInput.mustGetScriptPath()
	snapshotPath := checkInput.mustGetSnapshotScriptPath()
	cmd := fmt.Sprintf(
		"cp -rf '%s' '%s' && chmod 0755 '%s'",
		scriptPath,
		snapshotPath,
		snapshotPath,
	)
	_, err := module.RunShellCmd(h.ctx, username, "SnapshotHealthCheckScript", cmd, h.logOut)
	return err
}

// removeScriptSnapshotLocked removes the snapshot script.
func (h *healthCheckRunner) removeScriptSnapshotLocked() error {
	checkInput := h.checkInput()
	if checkInput == nil {
		return nil
	}
	snapshotPath := checkInput.mustGetSnapshotScriptPath()
	if _, err := os.Stat(snapshotPath); err == nil {
		cmd := fmt.Sprintf("rm -rf '%s'", snapshotPath)
		_, err := module.RunShellCmd(
			h.ctx,
			checkInput.username,
			"RemoveScriptSnapshot",
			cmd,
			h.logOut,
		)
		return err
	}
	return nil
}

// stopLocked cancels the periodic health check schedule. Caller must hold h.scheduleMutex.
func (h *healthCheckRunner) stopLocked() error {
	if h.scheduleCancel == nil {
		return nil
	}
	h.scheduleCancel()
	h.scheduleCancel = nil
	// Wait for the schedule to exit.
	for scheduler.GetInstance().IsScheduleActive(h.scheduleID) {
		time.Sleep(100 * time.Millisecond)
	}
	util.FileLogger().
		Infof(h.ctx, "Stopped health check schedule %s", h.scheduleID)
	h.publishLastOutput(nil)
	return h.removeScriptSnapshotLocked()
}

// publishLastOutput stores the last health check output and notifies all waiters.
func (h *healthCheckRunner) publishLastOutput(output *healthCheckOutput) {
	h.lastOutputPtr.Store(output)
	h.outputAvailableWaiter.Lock()
	defer h.outputAvailableWaiter.Unlock()
	h.outputAvailableWaiter.NotifyAll()
}

// scheduleLocked schedules the health check on a fixed interval.
// Lock must be held by the caller.
func (h *healthCheckRunner) scheduleLocked() error {
	stopSignal := make(chan struct{}, 1)
	// Create a new cancellable context for the schedule.
	ctx, cancel := context.WithCancel(h.ctx)
	h.scheduleCancel = cancel
	isIdleTimeoutReached := func(input *healthCheckInput) bool {
		idleTimeout := time.Duration(input.pbInput.GetIdleTimeoutSec()) * time.Second
		lastQueryTime := h.lastScheduleQueryTime()
		if time.Since(lastQueryTime) > idleTimeout {
			util.FileLogger().
				Infof(h.ctx, "Health check schedule idle timeout reached, stopping schedule")
			select {
			case stopSignal <- struct{}{}:
			default:
			}
			return true
		}
		return false
	}
	scheduleInterval := time.Duration(h.checkInput().pbInput.GetScheduleIntervalSec()) * time.Second
	util.FileLogger().
		Infof(h.ctx, "Scheduling health check with interval: %s", scheduleInterval)
	h.scheduleID = scheduler.GetInstance().Schedule(
		ctx,
		scheduleInterval,
		true, /* runImmediately */
		func(ctx context.Context) (any, error) {
			input := h.checkInput()
			// Check before running the health check to skip unnecessary work.
			if isIdleTimeoutReached(input) {
				h.publishLastOutput(&healthCheckOutput{idleTimedOut: true})
			} else {
				h.publishLastOutput(&healthCheckOutput{pbOutput: runCheck(ctx, input)})
				// Check after running the health check.
				isIdleTimeoutReached(input)
			}
			return nil, nil
		},
	)
	h.watchForStopSignalLocked(ctx, stopSignal)
	return nil
}

// watchForStopSignalLocked watches for the stopSignal and stops the schedule if it is reached.
// Lock must be held by the caller.
func (h *healthCheckRunner) watchForStopSignalLocked(
	ctx context.Context,
	stopSignal <-chan struct{},
) {
	scheduleID := h.scheduleID
	go func() {
		select {
		case <-ctx.Done():
			// Cancelled by stopLocked; cleanup is already in progress.
			return
		case <-stopSignal:
			h.scheduleMutex.Lock()
			defer h.scheduleMutex.Unlock()
			if h.scheduleID == scheduleID {
				h.stopLocked()
			}
		}
	}()
}

// isLastOutputValid returns true if the output is a usable check result.
func (h *healthCheckRunner) isLastOutputValid(
	req *pb.HealthCheckInput,
	output *healthCheckOutput,
) bool {
	if output == nil || output.idleTimedOut || output.pbOutput == nil {
		return false
	}
	pbOut := output.pbOutput
	// Same generation and check was run after the requested time or the result is
	// within the requested epoch deadline.
	return pbOut.GetGenerationId() == req.GetGenerationId() &&
		(pbOut.GetStartEpochSecs() >= h.lastScheduleQueryTime().Unix() ||
			pbOut.GetEndEpochSecs() >= req.GetMinResultEpochSecs())
}

// lastOutputBlocked returns a copy of the most recent health check result for the
// requested generation that is accepted by isLastOutputValid, or waits until one
// is available (or timeout/ctx cancel). Idle timeout wakes the wait and fails it.
// ctx is the RPC/task context so Abort cancels the wait promptly.
func (h *healthCheckRunner) lastOutputBlocked(
	ctx context.Context,
	req *pb.HealthCheckInput,
) (*pb.HealthCheckOutput, error) {
	// Returns the last output, true if it is valid, false if it is not valid, and
	// an error if there is an error.
	outputFunc := func() (*healthCheckOutput, bool, error) {
		output := h.lastOutput()
		if h.isLastOutputValid(req, output) {
			return output.clone(), true, nil
		}
		if output != nil && output.idleTimedOut {
			return nil, false, errors.New("Health check schedule idle timeout reached")
		}
		return nil, false, nil
	}
	h.outputAvailableWaiter.Lock()
	defer h.outputAvailableWaiter.Unlock()
	timedWaiter := h.outputAvailableWaiter.CreateTimeWaiter(
		ctx,
		time.Duration(req.GetRunTimeoutSec())*time.Second,
	)
	defer timedWaiter.Stop()
	iter := 0
	for {
		output, ok, err := outputFunc()
		if err != nil {
			return nil, err
		}
		if ok {
			if iter == 0 {
				util.FileLogger().
					Debug(h.ctx, "Valid health check output available without waiting")
			}
			return output.pbOutput, nil
		}
		iter++
		util.FileLogger().Debugf(h.ctx, "Waiting for health check output (iter: %d)", iter)
		if !timedWaiter.Wait() {
			// Deadline/cancel and publishLastOutput can race; re-check before failing.
			output, ok, err := outputFunc()
			if err != nil {
				return nil, err
			}
			if ok {
				return output.pbOutput, nil
			}
			if err := ctx.Err(); err != nil {
				util.FileLogger().Infof(h.ctx, "Health check wait cancelled: %v", err)
				return nil, err
			}
			util.FileLogger().Infof(h.ctx, "Health check schedule timed out")
			return nil, errors.New("Health check did not complete in time")
		}
	}
}

// runCheck runs the health check and sets the output.
// It does not hold the mutex because the input and the script path are not modified
// during the health check.
func runCheck(ctx context.Context, input *healthCheckInput) *pb.HealthCheckOutput {
	start := time.Now()
	snapshotPath := input.mustGetSnapshotScriptPath()
	args := buildHealthCheckCmd(snapshotPath, input.pbInput)
	timeout := time.Duration(input.pbInput.GetRunTimeoutSec()) * time.Second
	runCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	output := &pb.HealthCheckOutput{
		GenerationId:   input.pbInput.GetGenerationId(),
		StartEpochSecs: start.Unix(),
	}
	util.FileLogger().
		Debugf(ctx, "Running health check with args: %v", args)
	shellTask := NewShellTaskWithUser("runHealthCheck", input.username, util.DefaultShell, args)
	taskStatus, err := shellTask.Process(runCtx)
	if err != nil {
		code := 1
		message := err.Error()
		if taskStatus != nil && taskStatus.ExitStatus != nil {
			if taskStatus.ExitStatus.Code != 0 {
				code = taskStatus.ExitStatus.Code
			}
			if taskStatus.ExitStatus.Error != nil && taskStatus.ExitStatus.Error.Len() > 0 {
				message = taskStatus.ExitStatus.Error.String()
			}
		}
		if exitErr, ok := err.(*exec.ExitError); ok {
			code = exitErr.ExitCode()
		}
		output.Error = &pb.Error{Code: int32(code), Message: message}
		if taskStatus != nil && taskStatus.Info != nil && taskStatus.Info.Len() > 0 {
			output.ReportJson = taskStatus.Info.String()
		}
	} else if taskStatus != nil && taskStatus.Info != nil {
		output.ReportJson = taskStatus.Info.String()
	}
	output.EndEpochSecs = time.Now().Unix()
	return output
}

// buildHealthCheckCmd builds the command to run the health check.
func buildHealthCheckCmd(scriptPath string, input *pb.HealthCheckInput) []string {
	args := []string{scriptPath}
	if input.GetDdlAtomicityCheck() {
		args = append(args, "--ddl_atomicity_check=true")
		if input.GetMasterLeaderUrl() != "" {
			args = append(args, "--master_leader_url="+input.GetMasterLeaderUrl())
		}
	}
	if input.GetYbaYnpVersion() != "" {
		args = append(args, "--yba_ynp_version="+input.GetYbaYnpVersion())
	}
	return args
}

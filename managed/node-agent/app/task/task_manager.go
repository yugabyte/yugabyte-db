// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"io"
	"node-agent/app/executor"
	"node-agent/app/scheduler"
	pb "node-agent/generated/service"
	"node-agent/util"
	"sync"
	"time"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

const (
	// TaskPollInterval is the interval to check the running tasks.
	TaskPollInterval = time.Second * 3
	// TaskProgressWaitTime is the wait time to check the status of running task.
	TaskProgressWaitTime = time.Millisecond * 200
	// DefaultAsyncTaskTimeout is the maximum time an async task is allowed to run.
	DefaultAsyncTaskTimeout = time.Minute * 5
)

var (
	taskManager     *TaskManager
	onceTaskManager = &sync.Once{}
)

// TaskStatus carries information about the task progress.
type TaskStatus struct {
	// Info is a buffer to send any output to the client while the task is running.
	Info util.Buffer
	// ExitStatus is the final status of the task execution.
	ExitStatus *ExitStatus
}

// AsyncTask is the interface for a async task.
type AsyncTask interface {
	// Handle is the method to be executed.
	Handle(context.Context) (*pb.DescribeTaskResponse, error)
	// CurrentTaskStatus returns the current task status.
	// Nil can be returned if there is no status streaming for this task.
	CurrentTaskStatus() *TaskStatus
	// String returns the identifier for this task.
	String() string
}

// Handler converts AsyncTask Handle to the executor handler.
func ToHandler(handle func(context.Context) (*pb.DescribeTaskResponse, error)) util.Handler {
	return func(ctx context.Context) (any, error) {
		return handle(ctx)
	}
}

// ExitStatus stores the Error (if any) and the exit code of a command.
type ExitStatus struct {
	Error util.Buffer
	Code  int
}

// TaskCallbackData contains the progress data for a command.
type TaskCallbackData struct {
	Info        string
	Error       string
	ExitCode    int
	State       executor.TaskState
	RPCResponse *pb.DescribeTaskResponse
}

// TaskManager manages async tasks.
type TaskManager struct {
	ctx       context.Context
	taskInfos *sync.Map
}

type taskInfo struct {
	ctx       context.Context
	cancel    context.CancelFunc
	mutex     *sync.Mutex
	asyncTask AsyncTask
	future    *executor.Future
	updatedAt time.Time
}

// InitTaskManager initializes the task manager.
func InitTaskManager(ctx context.Context) *TaskManager {
	onceTaskManager.Do(func() {
		taskManager = &TaskManager{ctx: ctx, taskInfos: &sync.Map{}}
		taskExpirySecs := util.CurrentConfig().Int(util.NodeAgentTaskExpirySecsKey)
		// Start the scanner on a schedule.
		// Completed tasks are not immediately removed as the client can ask for status.
		scheduler.GetInstance().
			Schedule(ctx, TaskPollInterval, func(ctx context.Context) (any, error) {
				taskManager.taskInfos.Range(func(k any, v any) bool {
					taskID := k.(string)
					tInfo := v.(*taskInfo)
					select {
					case <-ctx.Done():
						tInfo.cancel()
					case <-tInfo.ctx.Done():
						taskManager.taskInfos.Delete(taskID)
						util.FileLogger().Infof(ctx, "Task %s is garbage collected.",
							taskID)
					default:
						tInfo.mutex.Lock()
						defer tInfo.mutex.Unlock()
						elapsed := int(time.Since(tInfo.updatedAt).Seconds())
						if elapsed > taskExpirySecs {
							tInfo.cancel()
							util.FileLogger().Infof(ctx, "Task %s is no longer tracked.",
								taskID)
						}
					}
					return true
				})
				return nil, nil
			})
	})
	return taskManager
}

// GetTaskManager returns the task manager instance.
func GetTaskManager() *TaskManager {
	if taskManager == nil {
		util.FileLogger().Fatal(context.TODO(), "Task manager is not initialized")
	}
	return taskManager
}

// updateTime updated the last updatedAt time.
func (m *TaskManager) updateTime(taskID string) {
	i, ok := m.taskInfos.Load(taskID)
	if !ok {
		return
	}
	tInfo := i.(*taskInfo)
	tInfo.mutex.Lock()
	defer tInfo.mutex.Unlock()
	tInfo.updatedAt = time.Now()
}

// SubmitTask submits a task for asynchronous execution.
func (m *TaskManager) Submit(
	ctx context.Context,
	taskID string,
	asyncTask AsyncTask) error {
	if taskID == "" {
		return errors.New("Task ID is not valid")
	}
	deadline, ok := ctx.Deadline()
	if !ok {
		deadline = time.Now().Add(DefaultAsyncTaskTimeout)
	}
	util.FileLogger().
		Infof(ctx, "Submitted task %s with timeout in %.2f secs", taskID, time.Until(deadline).Seconds())
	parentCtx := util.InheritContextKeys(ctx, context.Background())
	tInfoCtx, tInfoCancel := context.WithCancel(parentCtx)
	deadlineCtx, deadlineCancel := context.WithDeadline(tInfoCtx, deadline)
	cancelFnc := func() {
		deadlineCancel()
		tInfoCancel()
	}
	i, ok := m.taskInfos.LoadOrStore(
		taskID,
		&taskInfo{
			ctx:       tInfoCtx,
			cancel:    cancelFnc,
			mutex:     &sync.Mutex{},
			updatedAt: time.Now(),
			asyncTask: asyncTask,
		},
	)
	if ok {
		return fmt.Errorf("Task %s already exists", taskID)
	}
	tInfo := i.(*taskInfo)
	future, err := executor.GetInstance().SubmitTask(deadlineCtx, ToHandler(asyncTask.Handle))
	if err != nil {
		cancelFnc()
		m.taskInfos.Delete(taskID)
		util.FileLogger().
			Errorf(parentCtx, "Error in submitting command: %v - %s", asyncTask, err.Error())
		return err
	}
	tInfo.mutex.Lock()
	defer tInfo.mutex.Unlock()
	tInfo.future = future
	return nil
}

// Subscribe subscribes a callback for command output.
// It returns io.EOF when the task is completed and there is no more output.
func (m *TaskManager) Subscribe(
	ctx context.Context,
	taskID string,
	callback func(*TaskCallbackData) error,
) error {
	if taskID == "" {
		return errors.New("Task ID is not valid")
	}
	for {
		i, ok := m.taskInfos.Load(taskID)
		if !ok {
			return fmt.Errorf("Task %s is not found", taskID)
		}
		tInfo := i.(*taskInfo)
		if tInfo.future == nil {
			return fmt.Errorf("Invalid task %s for subscription", taskID)
		}
		select {
		case <-m.ctx.Done():
			return errors.New("Task manager is cancelled")
		case <-ctx.Done():
			m.updateTime(taskID)
			// Client is cancelled or deadline exceeded.
			// Return the right error code for the client to retry.
			if ctx.Err() == context.DeadlineExceeded {
				return status.New(codes.DeadlineExceeded, "Client deadline exceeded").Err()
			}
			return status.New(codes.Canceled, "Client cancelled request").Err()
		case <-tInfo.future.Done():
			// Task is completed.
			size := 0
			m.updateTime(taskID)
			taskStatus := tInfo.asyncTask.CurrentTaskStatus()
			if taskStatus != nil {
				callbackData := &TaskCallbackData{State: tInfo.future.State()}
				callbackData.Info, size = taskStatus.Info.StringWithLen()
				if size > 0 {
					// Send the info messages first before any error.
					err := callback(callbackData)
					if err != nil {
						return err
					}
					taskStatus.Info.Consume(size)
				}
			}

			size = 0
			exitCode := 0
			if taskStatus != nil && taskStatus.ExitStatus != nil {
				exitCode = taskStatus.ExitStatus.Code
			}
			if exitCode == 0 {
				result, err := tInfo.future.Get()
				if err != nil {
					exitCode = 1
					if status, ok := err.(*util.StatusError); ok {
						exitCode = status.Code()
					}
					callbackData := &TaskCallbackData{
						State:    tInfo.future.State(),
						ExitCode: exitCode,
						Error:    err.Error(),
					}
					err = callback(callbackData)
					if err != nil {
						return err
					}
				} else if response, ok := result.(*pb.DescribeTaskResponse); ok {
					callbackData := &TaskCallbackData{State: tInfo.future.State()}
					callbackData.RPCResponse = response
					err = callback(callbackData)
					if err != nil {
						return err
					}
				}
			} else {
				// Send the error messages.
				callbackData := &TaskCallbackData{State: tInfo.future.State()}
				callbackData.ExitCode = taskStatus.ExitStatus.Code
				callbackData.Error, size = taskStatus.ExitStatus.Error.StringWithLen()
				if size == 0 {
					if _, err := tInfo.future.Get(); err != nil {
						// Use the error as it may contain additional cancellation message.
						callbackData.Error = err.Error()
					}
				}
				err := callback(callbackData)
				if err != nil {
					return err
				}
				if size > 0 {
					taskStatus.ExitStatus.Error.Consume(size)
				}
			}
			util.FileLogger().Infof(ctx, "Task %s is completed.", taskID)
			return io.EOF
		default:
			// Task is still running.
			m.updateTime(taskID)
			taskStatus := tInfo.asyncTask.CurrentTaskStatus()
			if taskStatus != nil {
				data, size := taskStatus.Info.StringWithLen()
				if size > 0 {
					callbackData := &TaskCallbackData{Info: data, State: tInfo.future.State()}
					err := callback(callbackData)
					if err != nil {
						return err
					}
					taskStatus.Info.Consume(size)
				}
			}
			time.Sleep(TaskProgressWaitTime)
		}
	}
}

// Abort aborts a running task.
func (m *TaskManager) Abort(ctx context.Context, taskID string) (string, error) {
	i, ok := m.taskInfos.Load(taskID)
	if !ok {
		util.FileLogger().Infof(ctx, "Task %s is not found.", taskID)
		return "", nil
	}
	tInfo := i.(*taskInfo)
	select {
	case <-tInfo.future.Done():
		// It is already cancelled.
	default:
		tInfo.cancel()
	}
	return taskID, nil
}

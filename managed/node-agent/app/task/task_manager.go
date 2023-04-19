// Copyright (c) YugaByte, Inc.

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
)

const (
	// TaskPollInterval is the interval to check the running tasks.
	TaskPollInterval = time.Second * 3
	// TaskExpiry is the expiry after which a running task is cancelled.
	TaskExpiry = time.Second * 30
	// TaskProgressWaitTime is the wait time to check the status of running task.
	TaskProgressWaitTime = time.Millisecond * 200
)

var (
	taskManager     *TaskManager
	onceTaskManager = &sync.Once{}
)

// TaskStatus carries information about the task progress.
type TaskStatus struct {
	Info       util.Buffer
	ExitStatus *ExitStatus
}

// AsyncTask is the interface for a async task.
type AsyncTask interface {
	// Handler returns the method to be executed.
	Handler() util.Handler
	// CurrentTaskStatus returns the current task status.
	CurrentTaskStatus() *TaskStatus
	// String returns the identifier for this task.
	String() string
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
	ctx                  context.Context
	cancel               context.CancelFunc
	mutex                *sync.Mutex
	asyncTask            AsyncTask
	future               *executor.Future
	rpcResponseConverter util.RPCResponseConverter
	updatedAt            time.Time
}

// InitTaskManager initializes the task manager.
func InitTaskManager(ctx context.Context) *TaskManager {
	onceTaskManager.Do(func() {
		taskManager = &TaskManager{ctx: ctx, taskInfos: &sync.Map{}}
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
						elapsed := time.Now().Sub(tInfo.updatedAt)
						if elapsed > TaskExpiry {
							// Task has expired. Client has not asked for progress on this.
							tInfo.cancel()
							util.FileLogger().Infof(ctx, "Task %s is cancelled.",
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
		util.FileLogger().Fatal(nil, "Task manager is not initialized")
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
	asyncTask AsyncTask,
	rpcResponseConverter util.RPCResponseConverter,
) error {
	if taskID == "" {
		return errors.New("Task ID is not valid")
	}
	bgCtx, cancel := context.WithCancel(context.Background())
	correlationID := util.CorrelationID(ctx)
	if correlationID != "" {
		bgCtx = util.WithCorrelationID(bgCtx, correlationID)
	}
	i, ok := m.taskInfos.LoadOrStore(
		taskID,
		&taskInfo{
			ctx:                  bgCtx,
			cancel:               cancel,
			mutex:                &sync.Mutex{},
			updatedAt:            time.Now(),
			asyncTask:            asyncTask,
			rpcResponseConverter: rpcResponseConverter,
		},
	)
	if ok {
		return fmt.Errorf("Task %s already exists", taskID)
	}
	tInfo := i.(*taskInfo)
	future, err := executor.GetInstance().SubmitTask(bgCtx, asyncTask.Handler())
	if err != nil {
		m.taskInfos.Delete(taskID)
		util.FileLogger().
			Errorf(bgCtx, "Error in submitting command: %v - %s", asyncTask, err.Error())
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
			return errors.New("Subscriber is cancelled")
		case <-tInfo.future.Done():
			// Task is completed.
			size := 0
			m.updateTime(taskID)
			taskStatus := tInfo.asyncTask.CurrentTaskStatus()
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

			size = 0
			if taskStatus.ExitStatus.Code == 0 {
				if tInfo.rpcResponseConverter != nil {
					result, err := tInfo.future.Get()
					if err != nil {
						return err
					}
					response, err := tInfo.rpcResponseConverter(result)
					if err != nil {
						return err
					}
					callbackData := &TaskCallbackData{State: tInfo.future.State()}
					callbackData.RPCResponse = response
					err = callback(callbackData)
					if err != nil {
						return err
					}
				}
			} else {
				// Send the error messages.
				callbackData.ExitCode = taskStatus.ExitStatus.Code
				callbackData.Error, size = taskStatus.ExitStatus.Error.StringWithLen()
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
			data, size := taskStatus.Info.StringWithLen()
			if size > 0 {
				callbackData := &TaskCallbackData{Info: data, State: tInfo.future.State()}
				err := callback(callbackData)
				if err != nil {
					return err
				}
				taskStatus.Info.Consume(size)
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

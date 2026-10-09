// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"node-agent/app/executor"
	"node-agent/app/scheduler"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"reflect"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"google.golang.org/protobuf/proto"
)

var (
	healthCheckerTestInitOnce sync.Once
)

func initHealthCheckerTestEnv(t *testing.T) {
	t.Helper()
	healthCheckerTestInitOnce.Do(func() {
		ctx := context.Background()
		executor.Init(ctx)
		scheduler.Init(ctx)
	})
}

func newTestHealthCheckRunner() *healthCheckRunner {
	return &healthCheckRunner{
		ctx:                    context.Background(),
		scheduleMutex:          &sync.Mutex{},
		outputAvailableWaiter:  util.NewCondWaiter(context.Background()),
		inputPtr:               new(atomic.Pointer[healthCheckInput]),
		lastOutputPtr:          new(atomic.Pointer[healthCheckOutput]),
		lastClientQueryTimePtr: new(atomic.Pointer[time.Time]),
		logOut:                 util.NewBuffer(module.MaxBufferCapacity),
	}
}

func wrapHealthCheckOutput(pbOut *pb.HealthCheckOutput) *healthCheckOutput {
	return &healthCheckOutput{pbOutput: pbOut}
}

func validHealthCheckInput(ybHomeDir, generationID string) *pb.HealthCheckInput {
	return &pb.HealthCheckInput{
		GenerationId:        generationID,
		YbHomeDir:           ybHomeDir,
		ScheduleIntervalSec: 60,
		IdleTimeoutSec:      120,
		RunTimeoutSec:       5,
		MinResultEpochSecs:  time.Now().Add(-60 * time.Second).Unix(),
	}
}

func TestBuildHealthCheckCmd(t *testing.T) {
	scriptPath := "/home/yugabyte/bin/node_health_snapshot.py"

	t.Run("baseArgs", func(t *testing.T) {
		got := buildHealthCheckCmd(scriptPath, &pb.HealthCheckInput{})
		want := []string{scriptPath}
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("got %v, want %v", got, want)
		}
	})

	t.Run("ddlAtomicityWithMasterLeader", func(t *testing.T) {
		got := buildHealthCheckCmd(scriptPath, &pb.HealthCheckInput{
			DdlAtomicityCheck: true,
			MasterLeaderUrl:   "http://10.0.0.1:7000",
		})
		want := []string{
			scriptPath,
			"--ddl_atomicity_check=true",
			"--master_leader_url=http://10.0.0.1:7000",
		}
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("got %v, want %v", got, want)
		}
	})

	t.Run("ddlAtomicityWithoutMasterLeader", func(t *testing.T) {
		got := buildHealthCheckCmd(scriptPath, &pb.HealthCheckInput{
			DdlAtomicityCheck: true,
		})
		want := []string{scriptPath, "--ddl_atomicity_check=true"}
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("got %v, want %v", got, want)
		}
	})

	t.Run("ynpVersion", func(t *testing.T) {
		got := buildHealthCheckCmd(scriptPath, &pb.HealthCheckInput{
			YbaYnpVersion: "1.2.3",
		})
		want := []string{scriptPath, "--yba_ynp_version=1.2.3"}
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("got %v, want %v", got, want)
		}
	})
}

func TestHealthCheckInputScriptPaths(t *testing.T) {
	input := &healthCheckInput{
		pbInput: &pb.HealthCheckInput{YbHomeDir: "/home/yugabyte"},
	}
	if got, want := input.mustGetScriptPath(), "/home/yugabyte/bin/node_health.py"; got != want {
		t.Fatalf("script path: got %q, want %q", got, want)
	}
	if got, want := input.mustGetSnapshotScriptPath(), "/home/yugabyte/bin/node_health_snapshot.py"; got != want {
		t.Fatalf("snapshot path: got %q, want %q", got, want)
	}
}

func TestStartScheduleIfNeededValidation(t *testing.T) {
	runner := newTestHealthCheckRunner()
	ctx := context.Background()
	home := t.TempDir()

	cases := []struct {
		name  string
		input *pb.HealthCheckInput
		want  string
	}{
		{"nilInput", nil, "Input is not set"},
		{
			"missingGeneration",
			&pb.HealthCheckInput{
				YbHomeDir:           home,
				ScheduleIntervalSec: 1,
				IdleTimeoutSec:      1,
				RunTimeoutSec:       1,
				MinResultEpochSecs:  1,
			},
			"Generation ID is not set",
		},
		{
			"missingYbHome",
			&pb.HealthCheckInput{
				GenerationId:        "g1",
				ScheduleIntervalSec: 1,
				IdleTimeoutSec:      1,
				RunTimeoutSec:       1,
				MinResultEpochSecs:  1,
			},
			"YB home directory is not set",
		},
		{
			"missingSchedule",
			&pb.HealthCheckInput{
				GenerationId:       "g1",
				YbHomeDir:          home,
				IdleTimeoutSec:     1,
				RunTimeoutSec:      1,
				MinResultEpochSecs: 1,
			},
			"Schedule interval is not set",
		},
		{
			"missingIdle",
			&pb.HealthCheckInput{
				GenerationId:        "g1",
				YbHomeDir:           home,
				ScheduleIntervalSec: 1,
				RunTimeoutSec:       1,
				MinResultEpochSecs:  1,
			},
			"Idle timeout is not set",
		},
		{
			"missingRunTimeout",
			&pb.HealthCheckInput{
				GenerationId:        "g1",
				YbHomeDir:           home,
				ScheduleIntervalSec: 1,
				IdleTimeoutSec:      1,
				MinResultEpochSecs:  1,
			},
			"Run timeout is not set",
		},
		{
			"missingMinResultEpoch",
			&pb.HealthCheckInput{
				GenerationId:        "g1",
				YbHomeDir:           home,
				ScheduleIntervalSec: 1,
				IdleTimeoutSec:      1,
				RunTimeoutSec:       1,
			},
			"Min result epoch is not set",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			err := runner.startScheduleIfNeededBlocked(ctx, tc.input, "")
			if err == nil {
				t.Fatal("expected validation error")
			}
			if err.Error() != tc.want {
				t.Fatalf("got %q, want %q", err.Error(), tc.want)
			}
		})
	}
}

func TestRescheduleNeeded(t *testing.T) {
	initHealthCheckerTestEnv(t)
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")

	if !runner.shouldRescheduleLocked(input) {
		t.Fatal("expected reschedule when no input is stored")
	}

	runner.storeInput(input, "")
	if !runner.shouldRescheduleLocked(input) {
		// Schedule is not active for a zero schedule ID.
		t.Fatal("expected reschedule when schedule is inactive")
	}

	sameGen := validHealthCheckInput(t.TempDir(), "gen-1")
	differentGen := validHealthCheckInput(t.TempDir(), "gen-2")
	if !runner.shouldRescheduleLocked(differentGen) {
		t.Fatal("expected reschedule when generation changes")
	}
	// Same generation still needs schedule activity check; with inactive schedule it
	// remains true.
	if !runner.shouldRescheduleLocked(sameGen) {
		t.Fatal("expected reschedule while schedule is inactive")
	}
}

func TestLastOutputBlockedFreshCache(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")
	input.RunTimeoutSec = 2
	input.MinResultEpochSecs = time.Now().Add(-60 * time.Second).Unix()
	runner.storeLastClientQueryTime(time.Now())
	runner.storeInput(input, "")
	runner.publishLastOutput(wrapHealthCheckOutput(&pb.HealthCheckOutput{
		GenerationId: "gen-1",
		ReportJson:   `{"data":[]}`,
		EndEpochSecs: time.Now().Unix(),
	}))

	ctx := context.Background()
	out, err := runner.lastOutputBlocked(ctx, input)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if out.GetReportJson() != `{"data":[]}` {
		t.Fatalf("unexpected report: %s", out.GetReportJson())
	}
	if out.GetGenerationId() != "gen-1" {
		t.Fatalf("unexpected generation: %s", out.GetGenerationId())
	}
}

func TestLastOutputBlockedBelowFloorTimesOut(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")
	input.RunTimeoutSec = 1
	// Floor is "now"; cached end_epoch is in the past → reject and wait until timeout.
	// lastScheduleQueryTime is after StartEpochSecs so the start-based path does not apply.
	input.MinResultEpochSecs = time.Now().Unix()
	runner.storeLastClientQueryTime(time.Now())
	runner.storeInput(input, "")
	runner.publishLastOutput(wrapHealthCheckOutput(&pb.HealthCheckOutput{
		GenerationId:   "gen-1",
		ReportJson:     `{"data":[]}`,
		StartEpochSecs: time.Now().Add(-60 * time.Second).Unix(),
		EndEpochSecs:   time.Now().Add(-30 * time.Second).Unix(),
	}))

	_, err := runner.lastOutputBlocked(context.Background(), input)
	if err == nil {
		t.Fatal("expected timeout for cached output below min_result_epoch")
	}
	if err.Error() != "Health check did not complete in time" {
		t.Fatalf("got %q", err.Error())
	}
}

func TestLastOutputBlockedAcceptsAtFloor(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")
	input.RunTimeoutSec = 2
	floor := time.Now().Add(-10 * time.Second).Unix()
	input.MinResultEpochSecs = floor
	runner.storeLastClientQueryTime(time.Now())
	runner.storeInput(input, "")
	runner.publishLastOutput(wrapHealthCheckOutput(&pb.HealthCheckOutput{
		GenerationId: "gen-1",
		ReportJson:   `{"at":"floor"}`,
		EndEpochSecs: floor,
	}))

	out, err := runner.lastOutputBlocked(context.Background(), input)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if out.GetReportJson() != `{"at":"floor"}` {
		t.Fatalf("unexpected report: %s", out.GetReportJson())
	}
}

func TestLastOutputBlockedAcceptsByStartEpoch(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")
	input.RunTimeoutSec = 2
	// Floor rejects EndEpochSecs; StartEpochSecs after lastScheduleQueryTime must accept.
	queryTime := time.Now().Add(-5 * time.Second)
	input.MinResultEpochSecs = time.Now().Add(60 * time.Second).Unix()
	runner.storeLastClientQueryTime(queryTime)
	runner.storeInput(input, "")
	runner.publishLastOutput(wrapHealthCheckOutput(&pb.HealthCheckOutput{
		GenerationId:   "gen-1",
		ReportJson:     `{"by":"start"}`,
		StartEpochSecs: queryTime.Unix(),
		EndEpochSecs:   queryTime.Add(time.Second).Unix(),
	}))

	out, err := runner.lastOutputBlocked(context.Background(), input)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if out.GetReportJson() != `{"by":"start"}` {
		t.Fatalf("unexpected report: %s", out.GetReportJson())
	}
}

func TestLastOutputBlockedWrongGenerationTimesOut(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-2")
	input.RunTimeoutSec = 1
	input.MinResultEpochSecs = time.Now().Add(-60 * time.Second).Unix()
	runner.storeLastClientQueryTime(time.Now())
	runner.storeInput(input, "")
	runner.publishLastOutput(wrapHealthCheckOutput(&pb.HealthCheckOutput{
		GenerationId: "gen-1",
		ReportJson:   `{"data":[]}`,
		EndEpochSecs: time.Now().Unix(),
	}))

	_, err := runner.lastOutputBlocked(context.Background(), input)
	if err == nil {
		t.Fatal("expected timeout for mismatched generation")
	}
}

func TestLastOutputBlockedWaitsForFreshOutput(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")
	input.RunTimeoutSec = 5
	// Floor rejects the empty cache; waiter accepts the later sample at/after floor.
	input.MinResultEpochSecs = time.Now().Unix()
	runner.storeLastClientQueryTime(time.Now())
	runner.storeInput(input, "")

	go func() {
		time.Sleep(200 * time.Millisecond)
		runner.publishLastOutput(wrapHealthCheckOutput(&pb.HealthCheckOutput{
			GenerationId: "gen-1",
			ReportJson:   `{"fresh":true}`,
			EndEpochSecs: time.Now().Unix(),
		}))
	}()

	out, err := runner.lastOutputBlocked(context.Background(), input)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if out.GetReportJson() != `{"fresh":true}` {
		t.Fatalf("unexpected report: %s", out.GetReportJson())
	}
}

func TestLastOutputBlockedCancelsOnContext(t *testing.T) {
	runner := newTestHealthCheckRunner()
	input := validHealthCheckInput(t.TempDir(), "gen-1")
	// Long run timeout; cancel must win.
	input.RunTimeoutSec = 30
	input.MinResultEpochSecs = time.Now().Unix()
	runner.storeLastClientQueryTime(time.Now())
	runner.storeInput(input, "")

	ctx, cancel := context.WithCancel(context.Background())
	go func() {
		time.Sleep(50 * time.Millisecond)
		cancel()
	}()

	start := time.Now()
	_, err := runner.lastOutputBlocked(ctx, input)
	if err == nil {
		t.Fatal("expected error on context cancel")
	}
	if err != context.Canceled {
		t.Fatalf("got %v, want context.Canceled", err)
	}
	if elapsed := time.Since(start); elapsed > 2*time.Second {
		t.Fatalf("cancel did not unblock promptly: %v", elapsed)
	}
}

func TestHealthCheckerHandlerString(t *testing.T) {
	h := NewHealthCheckerHandler(&pb.HealthCheckInput{}, "yugabyte")
	if h.String() != "Health Check Task" {
		t.Fatalf("unexpected String(): %s", h.String())
	}
}

func createNodeHealthScriptHome(t *testing.T) string {
	t.Helper()
	home := t.TempDir()
	binDir := filepath.Join(home, "bin")
	if err := os.MkdirAll(binDir, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	// Invoked as `/bin/bash <script>`; keep the body bash-compatible.
	script := filepath.Join(binDir, nodeHealthScript)
	content := "echo '{\"data\":[]}'\n"
	if err := os.WriteFile(script, []byte(content), 0o755); err != nil {
		t.Fatalf("write script: %v", err)
	}
	return home
}

func TestStartScheduleAndReturnCachedOutput(t *testing.T) {
	initHealthCheckerTestEnv(t)
	runner := newTestHealthCheckRunner()
	home := createNodeHealthScriptHome(t)
	input := validHealthCheckInput(home, "gen-sched-1")
	input.ScheduleIntervalSec = 30
	input.IdleTimeoutSec = 60
	input.RunTimeoutSec = 10
	input.MinResultEpochSecs = time.Now().Add(-30 * time.Second).Unix()

	ctx := context.Background()
	// Match Handle: hold scheduleMutex across schedule + wait so a concurrent
	// reschedule cannot preempt an in-flight poll.
	runner.scheduleMutex.Lock()
	defer runner.scheduleMutex.Unlock()
	err := runner.startScheduleIfNeededBlocked(ctx, input, "")
	if err != nil {
		t.Fatalf("startScheduleIfNeededBlocked: %v", err)
	}
	t.Cleanup(func() {
		runner.scheduleMutex.Lock()
		defer runner.scheduleMutex.Unlock()
		_ = runner.stopLocked()
	})

	out, err := runner.lastOutputBlocked(ctx, input)
	if err != nil {
		t.Fatalf("lastOutputBlocked: %v", err)
	}
	if out.GetGenerationId() != "gen-sched-1" {
		t.Fatalf("generation: got %s", out.GetGenerationId())
	}
	if out.GetReportJson() == "" {
		t.Fatal("expected non-empty report json")
	}
	if out.GetError() != nil {
		t.Fatalf("unexpected check error: %v", out.GetError())
	}

	// Same generation should refresh non-generation fields without forcing a new schedule id.
	scheduleID := runner.scheduleID
	updated := proto.Clone(input).(*pb.HealthCheckInput)
	updated.MasterLeaderUrl = "http://127.0.0.1:7000"
	if err := runner.startScheduleIfNeededBlocked(ctx, updated, ""); err != nil {
		t.Fatalf("second startScheduleIfNeededBlocked: %v", err)
	}
	if runner.scheduleID != scheduleID {
		t.Fatalf("expected schedule id to stay %s, got %s", scheduleID, runner.scheduleID)
	}
	stored := runner.checkInput().pbInput
	if stored.GetMasterLeaderUrl() != "http://127.0.0.1:7000" {
		t.Fatalf("expected master leader url refresh, got %q", stored.GetMasterLeaderUrl())
	}

	// Generation change forces a reschedule; the previous schedule must become inactive.
	prevScheduleID := scheduleID
	reschedule := proto.Clone(input).(*pb.HealthCheckInput)
	reschedule.GenerationId = "gen-sched-2"
	if err := runner.startScheduleIfNeededBlocked(ctx, reschedule, ""); err != nil {
		t.Fatalf("reschedule startScheduleIfNeededBlocked: %v", err)
	}
	if runner.scheduleID == prevScheduleID {
		t.Fatal("expected a new schedule id after generation change")
	}
	if scheduler.GetInstance().IsScheduleActive(prevScheduleID) {
		t.Fatal("expected previous schedule to be inactive after generation change")
	}
	out2, err := runner.lastOutputBlocked(ctx, reschedule)
	if err != nil {
		t.Fatalf("lastOutputBlocked after reschedule: %v", err)
	}
	if out2.GetGenerationId() != "gen-sched-2" {
		t.Fatalf("expected gen-sched-2, got %s", out2.GetGenerationId())
	}
}

func TestIdleTimeoutStopsSchedule(t *testing.T) {
	initHealthCheckerTestEnv(t)
	runner := newTestHealthCheckRunner()
	home := createNodeHealthScriptHome(t)
	input := validHealthCheckInput(home, "gen-idle-1")
	input.ScheduleIntervalSec = 1
	input.IdleTimeoutSec = 1
	input.RunTimeoutSec = 5
	input.MinResultEpochSecs = time.Now().Add(-30 * time.Second).Unix()

	ctx := context.Background()
	runner.scheduleMutex.Lock()
	if err := runner.startScheduleIfNeededBlocked(ctx, input, ""); err != nil {
		runner.scheduleMutex.Unlock()
		t.Fatalf("startScheduleIfNeededBlocked: %v", err)
	}
	scheduleID := runner.scheduleID
	t.Cleanup(func() {
		runner.scheduleMutex.Lock()
		defer runner.scheduleMutex.Unlock()
		_ = runner.stopLocked()
	})

	if _, err := runner.lastOutputBlocked(ctx, input); err != nil {
		runner.scheduleMutex.Unlock()
		t.Fatalf("lastOutputBlocked: %v", err)
	}
	runner.scheduleMutex.Unlock()
	if !scheduler.GetInstance().IsScheduleActive(scheduleID) {
		t.Fatal("expected schedule active before idle timeout")
	}

	// Simulate no further YBA polls so the next tick trips idle timeout.
	// Mutex must be released so the idle-stop goroutine can call stopLocked.
	runner.storeLastClientQueryTime(time.Now().Add(-10 * time.Second))
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) && scheduler.GetInstance().IsScheduleActive(scheduleID) {
		time.Sleep(50 * time.Millisecond)
	}
	if scheduler.GetInstance().IsScheduleActive(scheduleID) {
		t.Fatal("expected idle timeout to stop the schedule")
	}

	runner.scheduleMutex.Lock()
	cancelNil := runner.scheduleCancel == nil
	runner.scheduleMutex.Unlock()
	if !cancelNil {
		t.Fatal("expected scheduleCancel cleared after idle stop")
	}

	snapshotPath := runner.checkInput().mustGetSnapshotScriptPath()
	if _, err := os.Stat(snapshotPath); !os.IsNotExist(err) {
		t.Fatalf("expected snapshot script removed after idle stop, err=%v", err)
	}
}

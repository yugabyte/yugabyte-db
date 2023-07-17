/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"sync"

	"github.com/apex/log"
	"github.com/apex/log/handlers/cli"
	"github.com/apex/log/handlers/logfmt"
	"gopkg.in/natefinch/lumberjack.v2"
)

type AppLogger struct {
	logger      *log.Logger
	enableDebug bool
}

var (
	consoleLogger *AppLogger
	fileLogger    *AppLogger

	onceConsoleLogger = &sync.Once{}
	onceFileLogger    = &sync.Once{}
)

// Returns the console logger.
func ConsoleLogger() *AppLogger {
	onceConsoleLogger.Do(func() {
		consoleLogger = &AppLogger{
			logger: &log.Logger{
				Handler: cli.New(os.Stdout),
				Level:   log.DebugLevel,
			},
			enableDebug: false,
		}
	})
	return consoleLogger
}

// Returns the file logger.
func FileLogger() *AppLogger {
	onceFileLogger.Do(func() {
		config := CurrentConfig()
		err := os.MkdirAll(LogsDir(), os.ModePerm)
		if err != nil {
			panic("Unable to create logs dir.")
		}
		logFilepath := filepath.Join(LogsDir(), config.String(NodeLoggerKey))
		writer := &lumberjack.Logger{
			Filename:   logFilepath,
			MaxSize:    config.Int(NodeAgentLogMaxMbKey),
			MaxBackups: config.Int(NodeAgentLogMaxBackupsKey),
			MaxAge:     config.Int(NodeAgentLogMaxDaysKey),
			Compress:   true,
		}
		fileLogger = &AppLogger{
			logger: &log.Logger{
				Handler: logfmt.New(writer),
				Level:   log.Level(config.Int(NodeAgentLogLevelKey)),
			},
			enableDebug: true,
		}
	})
	return fileLogger
}

func (l *AppLogger) getEntry(ctx context.Context) *log.Entry {
	entry := log.NewEntry(l.logger)
	if l.enableDebug {
		config := CurrentConfig()
		corrId := ""
		if ctx != nil {
			if v := ctx.Value(CorrelationId); v != nil {
				corrId = v.(string)
			}
		}
		// Get the line number from the runtime stack.
		funcPtr, file, line, ok := runtime.Caller(2)
		if ok {
			entry = entry.WithFields(
				log.Fields{
					"func": runtime.FuncForPC(funcPtr).Name(),
					"file": file,
					"line": line,
				},
			)
		}
		if version := config.String(PlatformVersionKey); version != "" {
			entry = entry.WithField("version", version)
		}
		if corrId != "" {
			entry = entry.WithField("corr", corrId)
		}
	}
	return entry
}

func (l *AppLogger) Error(ctx context.Context, msg string) {
	l.getEntry(ctx).Error(msg)
}

func (l *AppLogger) Errorf(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Errorf(msg, v...)
}

func (l *AppLogger) Info(ctx context.Context, msg string) {
	if l.IsInfoEnabled() {
		l.getEntry(ctx).Infof(msg)
	}
}

func (l *AppLogger) Infof(ctx context.Context, msg string, v ...interface{}) {
	if l.IsInfoEnabled() {
		l.getEntry(ctx).Infof(msg, v...)
	}
}

func (l *AppLogger) Debug(ctx context.Context, msg string) {
	if l.IsDebugEnabled() {
		l.getEntry(ctx).Debug(msg)
	}
}

func (l *AppLogger) Debugf(ctx context.Context, msg string, v ...interface{}) {
	if l.IsDebugEnabled() {
		l.getEntry(ctx).Debugf(msg, v...)
	}
}

func (l *AppLogger) Warn(ctx context.Context, msg string) {
	l.getEntry(ctx).Warn(msg)
}

func (l *AppLogger) Warnf(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Warnf(msg, v...)
}

func (l *AppLogger) Fatal(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Fatal(msg)
}

func (l *AppLogger) Fatalf(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Fatalf(msg, v...)
}

// IsDebugEnabled returns true only if debug is enabled.
func (l *AppLogger) IsDebugEnabled() bool {
	return l.IsLevelEnabled(log.DebugLevel)
}

// IsInfoEnabled returns true only if info is enabled.
func (l *AppLogger) IsInfoEnabled() bool {
	return l.IsLevelEnabled(log.InfoLevel)
}

// IsLevelEnabled returns true only if the given level is enabled.
func (l *AppLogger) IsLevelEnabled(level log.Level) bool {
	return int(l.logger.Level) <= int(level)
}

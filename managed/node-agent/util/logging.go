/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
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
		logFile := config.String(NodeLoggerKey)
		if logFile == "" {
			logFile = NodeAgentDefaultLog
		}
		logFilepath := filepath.Join(LogsDir(), logFile)
		writer := &lumberjack.Logger{
			Filename:   logFilepath,
			MaxSize:    500, // MB
			MaxBackups: 5,
			MaxAge:     15, // Days
			Compress:   true,
		}
		fileLogger = &AppLogger{
			logger: &log.Logger{
				Handler: logfmt.New(writer),
				Level:   1,
			},
			enableDebug: true,
		}
	})
	return fileLogger
}

func (l *AppLogger) getEntry() *log.Entry {
	entry := log.NewEntry(l.logger)
	if l.enableDebug {
		config := CurrentConfig()
		// Get the line number from the runtime stack.
		funcPtr, file, line, ok := runtime.Caller(2)
		if ok {
			entry = entry.WithFields(
				log.Fields{
					"function": runtime.FuncForPC(funcPtr).Name(),
					"file":     file,
					"line":     line,
				},
			)
		}
		if version := config.String(PlatformVersionKey); version != "" {
			entry.WithField("version", version)
		}
	}
	return entry
}
func (l *AppLogger) Errorf(msg string, v ...interface{}) {
	l.getEntry().Errorf(msg, v...)
}

func (l *AppLogger) Infof(msg string, v ...interface{}) {
	l.getEntry().Infof(msg, v...)
}

func (l *AppLogger) Error(msg string) {
	l.getEntry().Error(msg)
}

func (l *AppLogger) Info(msg string) {
	l.getEntry().Infof(msg)
}

func (l *AppLogger) Debug(msg string) {
	l.getEntry().Debug(msg)
}

func (l *AppLogger) Debugf(msg string, v ...interface{}) {
	l.getEntry().Debugf(msg, v...)
}

func (l *AppLogger) Warn(msg string) {
	l.getEntry().Warn(msg)
}

func (l *AppLogger) Warnf(msg string, v ...interface{}) {
	l.getEntry().Warnf(msg, v...)
}

func (l *AppLogger) Fatal(msg string, v ...interface{}) {
	l.getEntry().Fatal(msg)
}

func (l *AppLogger) Fatalf(msg string, v ...interface{}) {
	l.getEntry().Fatalf(msg, v...)
}

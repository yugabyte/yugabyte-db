/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
	"os"
	"runtime"
	"sync"

	"github.com/apex/log"
	"github.com/apex/log/handlers/cli"
	"github.com/apex/log/handlers/logfmt"
)

type AppLogger struct {
	logger *log.Logger
}

var (
	cliLogger      *log.Logger
	fileLogger     *AppLogger
	onceLoggerInit = &sync.Once{}
	config         *Config
)

//Initializes two loggers - CLI logger and File Only Logger.
func initLogger() {
	config = GetConfig()
	err := os.MkdirAll(GetLogsDir(), os.ModePerm)
	if err != nil {
		panic("Unable to create logs dir.")
	}
	var f *os.File

	//Look for Node Config path in the config and use the default logger if not present.
	if config.GetString(NodeLogger) != "" {
		f, err = os.OpenFile(
			GetLogsDir()+"/"+config.GetString(NodeLogger),
			os.O_APPEND|os.O_CREATE|os.O_WRONLY,
			0644,
		)
	} else {
		f, err = os.OpenFile(GetLogsDir()+"/"+NodeAgentDefaultLog, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	}

	if err != nil {
		panic("Unable to create or open log file.")
	}

	cliLogger = &log.Logger{
		Handler: cli.New(os.Stdout),
		Level:   1,
	}

	fileLogger = &AppLogger{
		logger: &log.Logger{
			Handler: logfmt.New(f),
			Level:   1,
		},
	}
}

//Returns CLI Logger.
//Returns nil if Loggers are not initialized
func getCliLogger() *log.Logger {
	onceLoggerInit.Do(func() {
		initLogger()
	})
	return cliLogger
}

//Returns File Logger.
//Returns nil if Loggers are not initialized.
func getFileLogger() *AppLogger {
	onceLoggerInit.Do(func() {
		initLogger()
	})
	return fileLogger
}

func (l *AppLogger) getEntry() *log.Entry {
	if config == nil {
		return log.NewEntry(l.logger)
	}
	//Get the line number from the Runtime stack
	function, file, line, ok := runtime.Caller(2)
	var entry *log.Entry
	if ok {
		entry = l.logger.WithFields(
			log.Fields{
				"version":  config.GetString(PlatformVersion),
				"function": runtime.FuncForPC(function),
				"file":     file,
				"line":     line,
			},
		)
	} else {
		entry = l.logger.WithFields(log.Fields{"version": config.GetString(PlatformVersion)})
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

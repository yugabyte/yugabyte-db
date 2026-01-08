/*
 * Copyright (c) YugabyteDB, Inc.
 */
package util

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"

	"github.com/apex/log"
	"github.com/apex/log/handlers/cli"
	"google.golang.org/grpc/grpclog"
	"gopkg.in/natefinch/lumberjack.v2"
)

// AppLogger is the logger interface.
type AppLogger interface {
	Error(ctx context.Context, msg string)
	Errorf(ctx context.Context, msg string, v ...interface{})
	Info(ctx context.Context, msg string)
	Infof(ctx context.Context, msg string, v ...interface{})
	Debug(ctx context.Context, msg string)
	Debugf(ctx context.Context, msg string, v ...interface{})
	Warn(ctx context.Context, msg string)
	Warnf(ctx context.Context, msg string, v ...interface{})
	Fatal(ctx context.Context, msg string, v ...interface{})
	Fatalf(ctx context.Context, msg string, v ...interface{})
	IsDebugEnabled(ctx context.Context) bool
	IsInfoEnabled(ctx context.Context) bool
	IsLevelEnabled(ctx context.Context, level log.Level) bool
}

// appLogger implements the AppLogger interface.
type appLogger struct {
	logger *log.Logger
	// enableDebugInfo indicates whether to log detailed info like file, line, func.
	enableDebugInfo bool
	// loadConfigFile indicates whether to load config file for more debug info.
	loadConfigFile bool
	// Path prefix upto node-agent.
	pathPrefix string
}

// multiAppLogger implements the AppLogger interface for multiple loggers.
type multiAppLogger struct {
	loggers []AppLogger
}

var (
	consoleLogger AppLogger
	fileLogger    AppLogger

	onceConsoleLogger = &sync.Once{}
	onceFileLogger    = &sync.Once{}

	// ContextKeys maps header to internal IDs.
	ContextKeys = map[string]ContextKey{
		CorrelationIdHeader:   CorrelationId,
		RequestIdHeader:       RequestId,
		RequestLogLevelHeader: RequestLogLevel,
	}
)

func NewMultiAppLogger(loggers ...AppLogger) AppLogger {
	return &multiAppLogger{
		loggers: loggers,
	}
}

// Returns the console logger.
func ConsoleLogger() AppLogger {
	onceConsoleLogger.Do(func() {
		consoleLogger = &appLogger{
			logger: &log.Logger{
				Handler: cli.New(os.Stdout),
				Level:   log.DebugLevel,
			},
			enableDebugInfo: false,
			loadConfigFile:  false,
		}
	})
	return consoleLogger
}

func setupGrpcLogger(config *Config) {
	logFilepath := filepath.Join(LogsDir(), config.String(NodeAgentGrpcLoggerKey))
	os.Setenv("GRPC_GO_LOG_SEVERITY_LEVEL", "INFO")
	os.Setenv("GRPC_GO_LOG_VERBOSITY_LEVEL", config.String(NodeAgentGrpcLogVerbosityKey))
	writer := &lumberjack.Logger{
		Filename:   logFilepath,
		MaxSize:    config.Int(NodeAgentGrpcLogMaxMbKey),
		MaxBackups: config.Int(NodeAgentGrpcLogMaxBackupsKey),
		MaxAge:     config.Int(NodeAgentGrpcLogMaxDaysKey),
		Compress:   true,
	}
	grpclog.SetLoggerV2(grpclog.NewLoggerV2(writer, writer, writer))
}

func createLogger(path string,
	maxSizeMB, maxBackups, maxAgeDays int,
	level log.Level, enableConsole bool,
	loadConfigFile bool,
) AppLogger {
	jLogger := &lumberjack.Logger{
		Filename:   path,
		MaxSize:    maxSizeMB,
		MaxBackups: maxBackups,
		MaxAge:     maxAgeDays,
		Compress:   true,
	}
	appLoggers := []AppLogger{}
	if enableConsole {
		consoleLogger = ConsoleLogger()
		appLoggers = append(appLoggers, consoleLogger)
	}
	appLogger := &appLogger{
		logger: &log.Logger{
			Handler: NewLogHandler(jLogger),
			Level:   level,
		},
		enableDebugInfo: true,
		loadConfigFile:  loadConfigFile,
	}
	if appLogger.pathPrefix == "" {
		_, file, _, ok := runtime.Caller(0)
		if ok {
			appLogger.pathPrefix = filepath.Dir(filepath.Dir(file))
		}
	}
	appLoggers = append(appLoggers, appLogger)
	return NewMultiAppLogger(appLoggers...)
}

func InitCustomAppLogger(
	path string,
	maxSizeMB, maxBackups, maxAgeDays int,
	level log.Level,
	enableConsole bool,
	loadConfigFile bool,
) AppLogger {
	onceFileLogger.Do(func() {
		fileLogger = createLogger(
			path,
			maxSizeMB,
			maxBackups,
			maxAgeDays,
			level,
			enableConsole,
			loadConfigFile,
		)
	})
	return fileLogger
}

// Returns the file logger.
func FileLogger() AppLogger {
	onceFileLogger.Do(func() {
		config := CurrentConfig()
		err := os.MkdirAll(LogsDir(), os.ModePerm)
		if err != nil {
			panic("Unable to create logs dir - " + err.Error())
		}

		setupGrpcLogger(config)
		logFilepath := filepath.Join(LogsDir(), config.String(NodeAgentLoggerKey))
		fileLogger = createLogger(
			logFilepath,
			config.Int(NodeAgentLogMaxMbKey),
			config.Int(NodeAgentLogMaxBackupsKey),
			config.Int(NodeAgentLogMaxDaysKey),
			log.Level(config.Int(NodeAgentLogLevelKey)),
			false, /* enableConsole */
			true,  /* loadConfigFile */
		)
	})
	return fileLogger
}

// effectiveLogLevel returns the log level from the context if set, else the logger's level.
func (l *appLogger) effectiveLogLevel(ctx context.Context) (bool, log.Level) {
	ifc := ctx.Value(RequestLogLevel)
	if ifc != nil {
		switch val := ifc.(type) {
		case string:
			i, err := strconv.Atoi(val)
			if err == nil {
				return true, log.Level(i)
			}
		}
	}
	return false, l.logger.Level
}

func (l *appLogger) getEntry(ctx context.Context) *log.Entry {
	lgr := l.logger
	if present, level := l.effectiveLogLevel(ctx); present && level != l.logger.Level {
		// Create a new logger with the effective log level to isolate this logger.
		lgr = &log.Logger{
			Handler: l.logger.Handler, /* Reuse the thread-safe handler */
			Level:   level,
		}
	}
	entry := log.NewEntry(lgr)
	if l.enableDebugInfo {
		// Get the line number from the runtime stack.
		funcPtr, file, line, ok := runtime.Caller(2)
		if ok {
			// Trim the unwanted path prefix.
			file = strings.TrimPrefix(file, l.pathPrefix)
			entry = entry.WithFields(
				log.Fields{
					"func": runtime.FuncForPC(funcPtr).Name(),
					"file": file,
					"line": line,
				},
			)
		}
		if ctx != nil {
			for _, val := range ContextKeys {
				if v := ctx.Value(val); v != nil && v != "" {
					entry = entry.WithField(string(val), v.(string))
				}
			}
		}
		if l.loadConfigFile {
			config := CurrentConfig()
			if version := config.String(PlatformVersionKey); version != "" {
				entry = entry.WithField("version", version)
			}
		}
	}
	return entry
}

func (l *appLogger) Error(ctx context.Context, msg string) {
	l.getEntry(ctx).Error(msg)
}

func (l *appLogger) Errorf(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Errorf(msg, v...)
}

func (l *appLogger) Info(ctx context.Context, msg string) {
	if l.IsInfoEnabled(ctx) {
		l.getEntry(ctx).Info(msg)
	}
}

func (l *appLogger) Infof(ctx context.Context, msg string, v ...interface{}) {
	if l.IsInfoEnabled(ctx) {
		l.getEntry(ctx).Infof(msg, v...)
	}
}

func (l *appLogger) Debug(ctx context.Context, msg string) {
	if l.IsDebugEnabled(ctx) {
		l.getEntry(ctx).Debug(msg)
	}
}

func (l *appLogger) Debugf(ctx context.Context, msg string, v ...interface{}) {
	if l.IsDebugEnabled(ctx) {
		l.getEntry(ctx).Debugf(msg, v...)
	}
}

func (l *appLogger) Warn(ctx context.Context, msg string) {
	l.getEntry(ctx).Warn(msg)
}

func (l *appLogger) Warnf(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Warnf(msg, v...)
}

func (l *appLogger) Fatal(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Fatal(msg)
}

func (l *appLogger) Fatalf(ctx context.Context, msg string, v ...interface{}) {
	l.getEntry(ctx).Fatalf(msg, v...)
}

// IsDebugEnabled returns true only if debug is enabled.
func (l *appLogger) IsDebugEnabled(ctx context.Context) bool {
	return l.IsLevelEnabled(ctx, log.DebugLevel)
}

// IsInfoEnabled returns true only if info is enabled.
func (l *appLogger) IsInfoEnabled(ctx context.Context) bool {
	return l.IsLevelEnabled(ctx, log.InfoLevel)
}

// IsLevelEnabled returns true only if the given level is enabled.
func (l *appLogger) IsLevelEnabled(ctx context.Context, level log.Level) bool {
	_, logLevel := l.effectiveLogLevel(ctx)
	return int(logLevel) <= int(level)
}

func (l *multiAppLogger) Error(ctx context.Context, msg string) {
	for _, logger := range l.loggers {
		logger.Error(ctx, msg)
	}
}

func (l *multiAppLogger) Errorf(ctx context.Context, msg string, v ...interface{}) {
	for _, logger := range l.loggers {
		logger.Errorf(ctx, msg, v...)
	}
}
func (l *multiAppLogger) Info(ctx context.Context, msg string) {
	for _, logger := range l.loggers {
		logger.Info(ctx, msg)
	}
}
func (l *multiAppLogger) Infof(ctx context.Context, msg string, v ...interface{}) {
	for _, logger := range l.loggers {
		logger.Infof(ctx, msg, v...)
	}
}
func (l *multiAppLogger) Debug(ctx context.Context, msg string) {
	for _, logger := range l.loggers {
		logger.Debug(ctx, msg)
	}
}
func (l *multiAppLogger) Debugf(ctx context.Context, msg string, v ...interface{}) {
	for _, logger := range l.loggers {
		logger.Debugf(ctx, msg, v...)
	}
}
func (l *multiAppLogger) Warn(ctx context.Context, msg string) {
	for _, logger := range l.loggers {
		logger.Warn(ctx, msg)
	}
}
func (l *multiAppLogger) Warnf(ctx context.Context, msg string, v ...interface{}) {
	for _, logger := range l.loggers {
		logger.Warnf(ctx, msg, v...)
	}
}
func (l *multiAppLogger) Fatal(ctx context.Context, msg string, v ...interface{}) {
	for _, logger := range l.loggers {
		logger.Fatal(ctx, msg, v...)
	}
}
func (l *multiAppLogger) Fatalf(ctx context.Context, msg string, v ...interface{}) {
	for _, logger := range l.loggers {
		logger.Fatalf(ctx, msg, v...)
	}
}

// IsDebugEnabled returns true only if debug is enabled.
func (l *multiAppLogger) IsDebugEnabled(ctx context.Context) bool {
	for _, logger := range l.loggers {
		if logger.IsDebugEnabled(ctx) {
			return true
		}
	}
	return false
}

// IsInfoEnabled returns true only if info is enabled.
func (l *multiAppLogger) IsInfoEnabled(ctx context.Context) bool {
	for _, logger := range l.loggers {
		if logger.IsInfoEnabled(ctx) {
			return true
		}
	}
	return false
}

// IsLevelEnabled returns true only if the given level is enabled.
func (l *multiAppLogger) IsLevelEnabled(ctx context.Context, level log.Level) bool {
	for _, logger := range l.loggers {
		if logger.IsLevelEnabled(ctx, level) {
			return true
		}
	}
	return false
}

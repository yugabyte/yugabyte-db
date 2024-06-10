package logger

import (
    "os"

    "go.uber.org/zap"
    "go.uber.org/zap/zapcore"
)

type ZapSugaredLogger struct {
    logger *zap.SugaredLogger
}

func NewSugaredLogger(logLevel LogLevel) (*ZapSugaredLogger, error) {

    level := zap.InfoLevel
    switch logLevel {
    case Debug:
        level = zap.DebugLevel
    case Info:
        level = zap.InfoLevel
    case Warn:
        level = zap.WarnLevel
    case Error:
        level = zap.ErrorLevel
    default:
        println("unknown log level when initializing logger, defaulting to info level logging")
    }

    encoderConfig := zap.NewProductionEncoderConfig()
    encoderConfig.EncodeTime = zapcore.ISO8601TimeEncoder
    encoderConfig.FunctionKey = "func"
    consoleEncoder := zapcore.NewConsoleEncoder(encoderConfig)

    consoleDebugging := zapcore.Lock(os.Stdout)
    consoleErrors := zapcore.Lock(os.Stderr)
    core := zapcore.NewTee(
        zapcore.NewCore(consoleEncoder, consoleDebugging, level),
        zapcore.NewCore(consoleEncoder, consoleErrors, zap.ErrorLevel),
    )
    zapLogger := zap.New(core)
    defer zapLogger.Sync()

    sugaredLogger := zapLogger.Sugar().WithOptions(
        zap.AddCallerSkip(1),
        zap.AddCaller(),
    )
    return &ZapSugaredLogger{sugaredLogger}, nil
}

func (zapLogger *ZapSugaredLogger) Debugf(format string, args ...interface{}) {
    zapLogger.logger.Debugf(format, args...)
}

func (zapLogger *ZapSugaredLogger) Infof(format string, args ...interface{}) {
    zapLogger.logger.Infof(format, args...)
}

func (zapLogger *ZapSugaredLogger) Warnf(format string, args ...interface{}) {
    zapLogger.logger.Warnf(format, args...)
}

func (zapLogger *ZapSugaredLogger) Errorf(format string, args ...interface{}) {
    zapLogger.logger.Errorf(format, args...)
}

func (zapLogger *ZapSugaredLogger) With(args ...interface{}) Logger {
    return &ZapSugaredLogger{zapLogger.logger.With(args...)}
}

func (zapLogger *ZapSugaredLogger) Cleanup() {
    zapLogger.logger.Sync()
}

// Ensure that Logger interface is implemented
var _ Logger = (*ZapSugaredLogger)(nil)

func NewLoggerImpl(logLevel LogLevel) (*ZapSugaredLogger, error) {
    return NewSugaredLogger(logLevel)
}

package logger

import (
    "os"

    "go.uber.org/zap"
    "go.uber.org/zap/zapcore"
)

type ZapSugaredLogger struct {
    logger *zap.SugaredLogger
}

func NewSugaredLogger(debugLevel bool) (*ZapSugaredLogger, error) {

    level := zap.InfoLevel
    if debugLevel {
        level = zap.DebugLevel
    }

    encoderConfig := zap.NewProductionEncoderConfig()
    encoderConfig.EncodeTime = zapcore.ISO8601TimeEncoder
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
    )
    return &ZapSugaredLogger{sugaredLogger}, nil
}

func (zapLogger *ZapSugaredLogger) Debugf(format string, args ...interface{}) {
    zapLogger.logger.Debugf(format, args...)
}

func (zapLogger *ZapSugaredLogger) Infof(format string, args ...interface{}) {
    zapLogger.logger.Infof(format, args...)
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
var Log, _ = NewSugaredLogger(false)
var DebugLog, _ = NewSugaredLogger(true)

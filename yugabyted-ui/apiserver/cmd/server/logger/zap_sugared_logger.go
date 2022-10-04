package logger

import (
    "go.uber.org/zap"
)

type ZapSugaredLogger struct {
    logger *zap.SugaredLogger
}

func NewSugaredLogger() (*ZapSugaredLogger, error) {
    zapLogger, err := zap.NewProduction()
    if err != nil {
        return nil, err
    }
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

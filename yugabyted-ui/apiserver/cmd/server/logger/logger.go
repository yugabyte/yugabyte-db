package logger

type LogLevel int64

const (
    Debug LogLevel = iota
    Info
    Warn
    Error
)

type Logger interface {
    // Standard format string logging functions
    Debugf(format string, args ...interface{})
    Infof(format string, args ...interface{})
    Warnf(format string, args ...interface{})
    Errorf(format string, args ...interface{})

    // Standard function for adding fields for structured logging
    With(args ...interface{}) Logger

    // Anything that needs to be run before the program exits
    Cleanup()
}

func NewLogger(logLevel LogLevel) (Logger, error) {
    return NewLoggerImpl(logLevel)
}

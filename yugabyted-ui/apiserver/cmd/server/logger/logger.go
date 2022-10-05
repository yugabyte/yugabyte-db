package logger

type Logger interface {
    // Standard format string logging functions
    Debugf(format string, args ...interface{})
    Infof(format string, args ...interface{})
    Errorf(format string, args ...interface{})

    // Standard function for adding fields for structured logging
    With(args ...interface{}) Logger

    // Anything that needs to be run before the program exits
    Cleanup()
}

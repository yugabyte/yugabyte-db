package logging

import (
	"fmt"
	"os"

	log "github.com/sirupsen/logrus"
)

// Fatal prints the error message to stdout at the error level, and
// then kills the currently running process.
func Fatal(errorMsg string) {
	log.Fatalln(errorMsg)
}

// Info prints the info message to the console at the info level.
func Info(infoMsg string) {
	log.Infoln(infoMsg)
}

// Warn will log a warning message.
func Warn(warnMsg string) {
	log.Warn(warnMsg)
}

// Debug prints the debug message to the console at the debug level.
func Debug(debugMsg string) {
	log.Debugln(debugMsg)
}

// Init sets up the logger according to the right level.
func Init(logLevel string) {

	// Currently only the log message with an info level severity or above are logged.
	// Change the log level to debug for more verbose logging output.
	log.SetFormatter(&log.TextFormatter{
		ForceColors:   true,
		DisableColors: false,
	})

	switch logLevel {
	case "trace":
		log.SetLevel(log.TraceLevel)
	case "debug":
		log.SetLevel(log.DebugLevel)
	case "info":
		log.SetLevel(log.InfoLevel)
	case "warn":
		log.SetLevel(log.WarnLevel)
	case "error":
		log.SetLevel(log.ErrorLevel)
	case "fatal":
		log.SetLevel(log.FatalLevel)
	case "panic":
		log.SetLevel(log.PanicLevel)
	default:
		log.Fatal(fmt.Sprintf("Invalid log level specified: [%s]", logLevel))

	}

	// TODO: Also make logging file for installer actions.
	log.SetOutput(os.Stdout)
}

package logging

import (
	"fmt"
	"io/ioutil"
	"os"

	log "github.com/sirupsen/logrus"
	"github.com/sirupsen/logrus/hooks/writer"
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

func AddOutputFile(filePath string) {
	logFile, err := os.OpenFile(filePath, os.O_CREATE|os.O_APPEND|os.O_RDWR, 0666)
	if err != nil {
		panic(err)
	}
	log.Infoln(fmt.Sprintf("Opened log file %s", filePath))

	// log file is always at trace level
	// set log file as a hook for all levels
	for level := log.PanicLevel; level <= log.TraceLevel; level++ {
		log.AddHook(&writer.Hook{
			Writer: logFile,
			LogLevels: []log.Level{
				level,
			},
		})
	}
}

// Init sets up the logger according to the right level.
func Init(logLevel string) {

	// Currently only the log message with an info level severity or above are logged.
	// Change the log level to debug for more verbose logging output.
	log.SetFormatter(&log.TextFormatter{
		ForceColors:   true,
		DisableColors: false,
		FullTimestamp: true,
	})

	stdErrLogLevel, err := log.ParseLevel(logLevel)
	if err != nil {
		println(fmt.Sprintf("Invalid log level specified, assuming info: [%s]", logLevel))
		stdErrLogLevel = log.InfoLevel
	}

	log.SetOutput(ioutil.Discard) // Send all logs to nowhere by default

	// set ourselves as a handler for each level below the specified level
	for level := log.PanicLevel; level <= stdErrLogLevel; level++ {
		log.AddHook(&writer.Hook{
			Writer: os.Stdout,
			LogLevels: []log.Level{
				level,
			},
		})
	}
}

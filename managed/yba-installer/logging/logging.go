package logging

import (
	"fmt"
	"io/ioutil"
	"os"
	"runtime"

	log "github.com/sirupsen/logrus"
	"github.com/sirupsen/logrus/hooks/writer"
)

// Fatal prints the error message to stdout at the error level, and
// then kills the currently running process.
func Fatal(errorMsg string) {
	stackTrace := make([]byte, 4096)
	count := runtime.Stack(stackTrace, false)
	log.Debug("Hit fatal error with stack trace: \n" + string(stackTrace[:count]) + "\n")
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

func Trace(msg string) {
	log.Traceln(msg)
}

func AddOutputFile(filePath string) {
	logFile, err := os.OpenFile(filePath, os.O_CREATE|os.O_APPEND|os.O_RDWR, 0666)
	if err != nil {
		log.Fatalln("Unable to create log file " + filePath)
	}
	log.Debugln(fmt.Sprintf("Opened log file %s", filePath))

	// log file is always at trace level
	levels := []log.Level{}
	for level := log.PanicLevel; level <= log.TraceLevel; level++ {
		levels = append(levels, level)
	}

	log.AddHook(&writer.Hook{
		Writer:    logFile,
		LogLevels: levels,
	})
}

// Init sets up the logger according to the right level.
func Init(logLevel string) {

	// TODO: use different formatters for tty and log file, similar to
	// https://github.com/sirupsen/logrus/issues/894#issuecomment-1284051207
	log.SetFormatter(&log.TextFormatter{
		ForceColors:            true, // without this, logrus logs in logfmt output by default
		FullTimestamp:          true,
		DisableLevelTruncation: true,
	})

	log.SetLevel(log.TraceLevel)

	stdErrLogLevel, err := log.ParseLevel(logLevel)
	if err != nil {
		println(fmt.Sprintf("Invalid log level specified, assuming info: [%s]", logLevel))
		stdErrLogLevel = log.InfoLevel
	}

	log.SetOutput(ioutil.Discard) // Send all logs to nowhere by default

	// set ourselves as a handler for each level below the specified level
	levels := []log.Level{}
	for level := log.PanicLevel; level <= stdErrLogLevel; level++ {
		levels = append(levels, level)
	}

	log.AddHook(&writer.Hook{
		Writer:    os.Stdout,
		LogLevels: levels,
	})

}

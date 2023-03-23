package logging

import (
	"fmt"
	"os"
	"runtime"

	log "github.com/sirupsen/logrus"
)

var logFileLogger = log.New()
var stdLogger = log.New()

// Fatal prints the error message to stdout at the error level, and
// then kills the currently running process.
func Fatal(errorMsg string) {
	stackTrace := make([]byte, 4096)
	count := runtime.Stack(stackTrace, false)
	Debug("Hit fatal error with stack trace: \n" + string(stackTrace[:count]) + "\n")
	logFileLogger.Warn(errorMsg) // only warn level so we can log to both file and stdout
	stdLogger.Fatalln(errorMsg)
}

func Error(errorMsg string) {
	logFileLogger.Errorln(errorMsg)
	stdLogger.Errorln(errorMsg)
}

// Info prints the info message to the console at the info level.
func Info(infoMsg string) {
	logFileLogger.Infoln(infoMsg)
	stdLogger.Infoln(infoMsg)
}

// Warn will log a warning message.
func Warn(warnMsg string) {
	logFileLogger.Warn(warnMsg)
	stdLogger.Warn(warnMsg)
}

// Debug prints the debug message to the console at the debug level.
func Debug(debugMsg string) {
	logFileLogger.Debug(debugMsg)
	stdLogger.Debugln(debugMsg)
}

// DebugLF will debug log only to the file logger.
func DebugLF(debugMsg string) {
	logFileLogger.Debug(debugMsg)
}

func Trace(msg string) {
	logFileLogger.Trace(msg)
	stdLogger.Traceln(msg)
}

// AddOutputFile adds a logging file
func AddOutputFile(logfile string) {
	logFile, err := os.OpenFile(logfile, os.O_CREATE|os.O_APPEND|os.O_RDWR, 0666)
	if err != nil {
		log.Fatalln("Unable to create log file " + logfile)
	}

	stdLogger.Debugln(fmt.Sprintf("Opened log file %s", logfile))

	logFileLogger.SetFormatter(&log.TextFormatter{
		DisableColors: true,
		FullTimestamp: true,
		DisableQuote:  true, // needed for newlines to print in log file
	})

	logFileLogger.SetLevel(log.TraceLevel)
	logFileLogger.SetOutput(logFile)

}

// Init sets up the logger according to the right level.
func Init(logLevel string) {

	stdLogger.SetFormatter(&log.TextFormatter{
		ForceColors:            true, // without this, logrus logs in logfmt output by default
		FullTimestamp:          true,
		DisableLevelTruncation: true,
	})

	stdErrLogLevel, err := log.ParseLevel(logLevel)
	if err != nil {
		println(fmt.Sprintf("Invalid log level specified, assuming info: [%s]", logLevel))
		stdErrLogLevel = log.InfoLevel
	}

	stdLogger.SetLevel(stdErrLogLevel)
	stdLogger.SetOutput(os.Stdout) // Send all logs to nowhere by default

}

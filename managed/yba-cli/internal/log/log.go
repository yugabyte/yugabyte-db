/*
 * Copyright (c) YugaByte, Inc.
 */

package log

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	easy "github.com/t-tomalak/logrus-easy-formatter"
)

// SetFormatter sets log formatter for logrus
func SetFormatter() {
	logrus.SetFormatter(&easy.Formatter{
		LogFormat: "%msg%",
	})
}

// SetDebugFormatter sets log formatter for logrus debug
func SetDebugFormatter() {
	logrus.SetReportCaller(true)
	logrus.SetFormatter(&logrus.TextFormatter{
		DisableColors:          viper.GetBool("no-color"),
		DisableLevelTruncation: true,
		CallerPrettyfier: func(f *runtime.Frame) (string, string) {
			return "", fmt.Sprintf("%s:%d", filepath.Base(f.File), f.Line)
		},
	})
}

// SetLogLevel sets log level for logrus
func SetLogLevel(logLevel string, debug bool) {

	if debug {
		logrus.SetLevel(logrus.DebugLevel)
		SetDebugFormatter()
		return
	}
	if logLevel != "" {
		level, err := logrus.ParseLevel(logLevel)
		if err != nil {
			logrus.Errorln()
			fmt.Fprintf(os.Stderr, "Error Parsing Log level: %s\n", logLevel)
			os.Exit(1)
		}
		logrus.SetLevel(level)
	} else {
		logrus.SetLevel(logrus.InfoLevel)
	}
}

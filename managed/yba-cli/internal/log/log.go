/*
 * Copyright (c) YugaByte, Inc.
 */

package log

import (
	"fmt"
	"path/filepath"
	"runtime"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	easy "github.com/t-tomalak/logrus-easy-formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// setFormatter sets log formatter for logrus
func setFormatter() {
	logrus.SetFormatter(&easy.Formatter{
		LogFormat: "%msg%",
	})
}

// setDebugFormatter sets log formatter for logrus debug
func setDebugFormatter() {
	logrus.SetReportCaller(true)
	logrus.SetFormatter(&logrus.TextFormatter{
		DisableColors:          viper.GetBool("disable-color"),
		DisableLevelTruncation: true,
		FullTimestamp:          true,
		CallerPrettyfier: func(f *runtime.Frame) (string, string) {
			return "", fmt.Sprintf("%s:%d", filepath.Base(f.File), f.Line)
		},
	})
}

// SetLogLevel sets log level for logrus
func SetLogLevel(logLevel string, debug bool) {

	setFormatter()
	if debug {
		logrus.SetLevel(logrus.DebugLevel)
		setDebugFormatter()
		return
	}
	if logLevel != "" {
		level, err := logrus.ParseLevel(logLevel)
		if err != nil {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf("Error Parsing Log level: %s\n", logLevel),
				formatter.RedColor,
			))
		}
		logrus.SetLevel(level)
	} else {
		logrus.SetLevel(logrus.InfoLevel)
	}
}

// Copyright (c) YugabyteDB, Inc.

package config

import (
	"context"
	"log"
	"node-agent/util"
	"os"
	"path/filepath"

	apex "github.com/apex/log"
)

const (
	DefaultLogFile    = "app.log"
	DefaultLogDir     = "./logs"
	DefaultMaxSizeMB  = 10
	DefaultMaxBackups = 5
	DefaultMaxAgeDays = 30
)

func SetupLogger(ctx context.Context, config map[string]map[string]any) {
	// Default values
	logFile := DefaultLogFile
	logDir := DefaultLogDir
	logLevel := apex.DebugLevel
	logging, ok := config["logging"]
	if ok {
		if lf, ok := logging["file"].(string); ok && lf != "" {
			logFile = lf
		}
		if ld, ok := logging["directory"].(string); ok && ld != "" {
			logDir = ld
		}
		if ll, ok := logging["level"].(string); ok && ll != "" {
			level, err := apex.ParseLevel(ll)
			if err == nil {
				logLevel = level
			} else {
				log.Printf("Invalid log level '%s', defaulting to 'info'", ll)
			}
		}
	}
	// Create log directory if it doesn't exist
	err := os.MkdirAll(logDir, 0755)
	if err != nil {
		log.Fatalf("Failed to create log directory: %v", err)
	}
	logPath := filepath.Join(logDir, logFile)
	util.InitCustomAppLogger(
		logPath,
		DefaultMaxSizeMB,
		DefaultMaxBackups,
		DefaultMaxAgeDays,
		logLevel,
		true,  /* enableConsole */
		false, /* loadConfigFile */
	)
	// Set file permissions
	_ = os.Chmod(logPath, 0644)
	// Set ownership to original user if run with sudo
	origUser := os.Getenv("SUDO_USER")
	userInfo, err := util.UserInfo(origUser)
	if err == nil && userInfo.CurrentUserID == 0 && !userInfo.IsCurrent {
		// Change ownership only if running as root and yb_user is different from current user.
		os.Chown(logDir, int(userInfo.UserID), int(userInfo.GroupID))
		os.Chown(logPath, int(userInfo.UserID), int(userInfo.GroupID))
	}
	util.FileLogger().Infof(ctx, "Logging setup complete in UTC timezone. Level: %s, path: %s",
		logLevel.String(), logPath)
}

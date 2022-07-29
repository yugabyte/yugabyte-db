package util

import (
	"context"

	"github.com/apex/log"
	"github.com/google/uuid"
)

var (
	CliLogger  *log.Logger
	FileLogger *AppLogger
)

type Handler func(context.Context) (any, error)

func InitCommonLoggers() {
	CliLogger = getCliLogger()
	FileLogger = getFileLogger()
}

func NewUUID() uuid.UUID {
	return uuid.New()
}

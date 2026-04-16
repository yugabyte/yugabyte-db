package checks

import (
	"fmt"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/components"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var ServicesRunningCheck *servicesRunningCheck = &servicesRunningCheck{
	CheckName:   "services_running",
	skipAllowed: true,
	Services:    []components.Service{},
}

func SetServicesRunningCheck(services []components.Service) {
	logging.Debug("setting services running check: " + fmt.Sprint(services))
	ServicesRunningCheck.Services = services
}

type servicesRunningCheck struct {
	CheckName   string
	skipAllowed bool
	Services    []components.Service
}

func (s *servicesRunningCheck) Name() string {
	return s.CheckName
}

func (s *servicesRunningCheck) SkipAllowed() bool {
	return s.skipAllowed
}

func (s *servicesRunningCheck) Execute() Result {
	res := Result{
		Check:  s.CheckName,
		Status: StatusPassed,
	}

	failedServices := make([]string, 0)
	for _, service := range s.Services {
		if service.Name() == "yb-logrotate" {
			logging.Debug("Skipping yb-logrotate service in services running check")
			continue // Skip yb-logrotate service in services running check
		}
		status, err := service.Status()
		if err != nil {
			logging.Error(fmt.Sprintf("Failed to get %s status: %s", service.Name(), err.Error()))
			failedServices = append(failedServices, service.Name())
			continue
		}
		if !common.IsHappyStatus(status) {
			logging.Error(fmt.Sprintf("%s has bad status %s", service.Name(), status.Status))
			failedServices = append(failedServices, service.Name())
		}
	}
	if len(failedServices) > 0 {
		res.Error = fmt.Errorf("services '%s' are not running", strings.Join(failedServices, ", "))
		res.Status = StatusCritical
	}

	return res
}

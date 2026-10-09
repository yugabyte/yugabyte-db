package components

import (
	"fmt"
	"iter"
	"slices"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

type Manager struct {
	services map[string]Service
}

func NewManager() *Manager {
	return &Manager{
		services: make(map[string]Service),
	}
}

func (m *Manager) RegisterService(s Service) {
	if _, ok := m.services[s.Name()]; ok {
		panic("Service with name " + s.Name() + " already registered")
	}
	m.services[s.Name()] = s
}

func (m *Manager) Services() iter.Seq[Service] {
	return func(yield func(Service) bool) {
		for s := range m.servicesInOrder(false /* reverse */) {
			if !yield((s)) {
				return
			}
		}
	}
}

func (m *Manager) ServicesReverse() iter.Seq[Service] {
	return func(yield func(Service) bool) {
		counter := len(m.services)
		for s := range m.servicesInOrder(true /* reverse */) {
			logging.Debug(fmt.Sprintf("Yielding service %d/%d: %s", counter, len(m.services), s.Name()))
			counter--
			if !yield(s) {
				return
			}
		}
	}
}

func (m *Manager) ReplicatedServices() iter.Seq[ReplicatedService] {
	return func(yield func(ReplicatedService) bool) {
		for s := range m.servicesInOrder(false /* reverse */) {
			if !s.IsReplicated() {
				panic(fmt.Sprintf("attempting to use non-replicated service %s as replicated", s.Name()))
			}
			if !yield(convertToReplicated(s)) {
				return
			}
		}
	}
}

func (m *Manager) ServiceByName(name string) Service {
	service, ok := m.services[name]
	if !ok {
		return nil
	}
	return service
}

// Enabled reports whether yba-ctl.yml makes the service called name part of the install.
// ServiceByName also returns services that are registered but disabled.
func (m *Manager) Enabled(name string) bool {
	return slices.Contains(m.serviceOrder(), name)
}

func (m *Manager) servicesInOrder(reverse bool) iter.Seq[Service] {
	return func(yield func(Service) bool) {
		counter := 1
		order := m.serviceOrder()
		if reverse {
			slices.Reverse(order)
		}
		for _, name := range order {
			service, ok := m.services[name]
			if !ok {
				logging.Debug("services registered in manager: " + fmt.Sprintf("%v", m.services))
				panic("Service with name " + name + " not found in manager")
			}
			logging.Debug(fmt.Sprintf("Yielding service %d/%d: %s", counter, len(m.services), service.Name()))
			counter++
			if !yield(service) {
				return
			}
		}
	}
}

func (m *Manager) serviceOrder() []string {
	order := []string{}
	if viper.GetBool("postgres.install.enabled") {
		order = append(order, "postgres")
	}
	order = append(order, "prometheus")
	order = append(order, "yb-platform")
	// yb-perf-advisor is optional and can run alongside yb-platform if it is enabled
	if viper.GetBool("perfAdvisor.enabled") {
		order = append(order, "yb-perf-advisor")
	}
	if viper.GetBool("nodeExporter.enabled") {
		order = append(order, "node-exporter")
	}
	// byoc-api-proxy is only enabled in specific BYOC deployments
	if viper.GetBool("byocApiProxy.enabled") {
		order = append(order, "byoc-api-proxy")
	}

	// Logrotate should be last
	order = append(order, "yb-logrotate")
	return order
}

func convertToReplicated(s Service) ReplicatedService {
	if service, ok := s.(ReplicatedService); ok {
		return service
	}
	panic(fmt.Sprintf("Service %s does not implement %T", s.Name(), (*ReplicatedService)(nil)))
}

// Basic service interface that all components should implement.
type Service interface {
	Name() string
	IsReplicated() bool
	Version() string
	Initialize() error
	Install() error
	Uninstall(cleaData bool) error
	PreUpgrade() error
	// Upgrade moves an installed service to the version in this bundle. Services that yba-ctl.yml
	// can enable after install must also handle Upgrade as their first install.
	Upgrade() error
	Status() (common.Status, error)
	Start() error
	Stop() error
	Restart() error
	Reconfigure() error
	TemplateFile() string
}

// Services that support replicated migration
type ReplicatedService interface {
	Service
	MigrateFromReplicated() error
	FinishReplicatedMigrate() error
}

package replicatedctl

import (
	"encoding/json"
	"fmt"
)

// AppInspectResult contains data on a single app returned from app inspect
type AppInspectResult struct {
	ID   string `json:"ID"`
	Name string `json:"Name"`
}

// AppInspect gets replicated app information
// CMD: replicatedctl app inspect
func (r *ReplicatedCtl) AppInspect() ([]AppInspectResult, error) {
	var results []AppInspectResult
	raw, err := r.run("app", "inspect")
	if err != nil {
		return results, fmt.Errorf("could not inspect replicated apps: %w", err)
	}
	err = json.Unmarshal(raw, &results)
	if err != nil {
		return results, fmt.Errorf("could not parse app inspect return: %w", err)
	}
	return results, nil
}

// AppStatusResult is the status of a single app returned from app status
type AppStatusResult struct {
	ID            string `json:"AppID"`
	State         string `json:"State"`
	DesiredState  string `json:"DesiredState"`
	Error         string `json:"Error"`
	Transitioning bool   `json:"IsTransitioning"`
}

// AppStatus gets the status of all replicated apps
// CMD: replicatedctl app status
func (r *ReplicatedCtl) AppStatus() ([]AppStatusResult, error) {
	var status []AppStatusResult
	raw, err := r.run("app", "status")
	if err != nil {
		return status, fmt.Errorf("could not inspect replicated apps: %w", err)
	}
	err = json.Unmarshal(raw, &status)
	if err != nil {
		return status, fmt.Errorf("could not parse app inspect return: %w", err)
	}
	return status, nil
}

// AppStop will stop replicated apps
// CMD: replicatedctl app stop
func (r *ReplicatedCtl) AppStop() error {
	_, err := r.run("app", "stop")
	if err != nil {
		return fmt.Errorf("could not stop replicated apps: %w", err)
	}
	return nil
}

// AppStart will start replicated apps
// CMD: replicatedctl app start
func (r *ReplicatedCtl) AppStart() error {
	_, err := r.run("app", "start")
	if err != nil {
		return fmt.Errorf("could not start replicated apps: %w", err)
	}
	return nil
}

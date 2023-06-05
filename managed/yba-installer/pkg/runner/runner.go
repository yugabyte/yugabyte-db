package runner

import (
	"errors"
	"fmt"
	"strconv"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

var (
	RunnerInjectionError = errors.New("error injected")
	RunnerCompletedError = errors.New("runner has already completed")
)

// Runner is a wrapper with extra context to execute a "step" function during
// yba-installer workflows.
type Runner struct {
	// Name of the runner, used for logging
	Name string
	// Track if the runner has completed
	Completed bool

	// Count the total number of steps
	counter int

	// Used to track sections (like install platform, install postgres)
	section        string
	sectionCounter int

	// Track how many times an specific id has been run.
	*idCounter
}

// New creates a new Runner with basic initializations.
func New(name string) *Runner {
	return &Runner{
		Name:      name,
		counter:   0,
		Completed: false,

		section:        "",
		sectionCounter: 0,

		idCounter: newIdCounter(),
	}
}

// StartSection begins a new "section" in the Runner. Useful for more detailed logging.
func (r *Runner) StartSection(section string) {
	if r.section != "" {
		log.Info("Completed section " + r.section + " in " + strconv.Itoa(r.sectionCounter) + " steps")
	}
	r.section = section
	r.sectionCounter = 0
	log.Info("Starting section " + r.section)
}

// RunStep will run a "step" - a function that returns an error
func (r *Runner) RunStep(f func() error) error {
	id := r.getID(f)

	// Initial validation
	if r.Completed {
		return fmt.Errorf("cannot run %s: %w", id, RunnerCompletedError)
	}
	if r.failEarly(id) {
		return fmt.Errorf("error on %s: %w", id, RunnerInjectionError)
	}

	// Increment our counters
	r.counter++
	r.sectionCounter++

	// Run the step and log
	log.Debug(fmt.Sprintf("running step %d: %s", r.counter, id))
	err := f()
	if err != nil {
		err = fmt.Errorf("%s failed %w", r.fmtErrPrefix(id), err)
		log.Error("Failed on " + id + ": " + err.Error())
		return err
	}
	log.Debug("Succeeded " + id)
	return nil
}

// EndSection will complete the active section.
// This is optional, as StartSection will inherently end the previous section.
func (r *Runner) EndSection() {
	log.Info("Completed section " + r.section + " in " + strconv.Itoa(r.sectionCounter) + " steps")
	r.section = ""
	r.sectionCounter = 0
}

// Complete will mark the runner as complete. It will not be useable after
func (r *Runner) Complete() {
	if r.section != "" {
		log.Info("Completed section " + r.section + " in " + strconv.Itoa(r.sectionCounter) + " steps")
	}

	log.Info("Completed runner " + r.Name + " in " + strconv.Itoa(r.counter) + " steps")
	r.Completed = true
}

func (r Runner) fmtErrPrefix(id string) string {
	if r.section != "" {
		return fmt.Sprintf("%s:%s-%s", r.Name, r.section, id)
	}
	return fmt.Sprintf("%s-%s", r.Name, id)
}

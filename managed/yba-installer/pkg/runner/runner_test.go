package runner

import "testing"

func TestStepCounting(t *testing.T) {
	r := New("test runner")
	r.StartSection("1")

	r.RunStep(helper)
	r.RunStep(helper)
	if r.sectionCounter != 2 {
		t.Errorf("section counter got %d, expected 2", r.sectionCounter)
	}
	if r.counter != 2 {
		t.Errorf("counter got %d, expected 2", r.counter)
	}
	r.StartSection("2")
	r.RunStep(helper)
	r.RunStep(helper)
	if r.sectionCounter != 2 {
		t.Errorf("section counter got %d, expected 2", r.sectionCounter)
	}
	if r.counter != 4 {
		t.Errorf("counter got %d, expected 4", r.counter)
	}
}

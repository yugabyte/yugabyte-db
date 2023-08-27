package runner

import (
	"fmt"
	"os"
	"testing"
)

func TestLoadFailEarlyFile(t *testing.T) {
	data := "1.step-goes.here"
	bData := []byte(data)
	err := os.WriteFile(failEarlyFile, bData, 0755)
	defer os.Remove(failEarlyFile)
	if err != nil {
		t.Skipf("Skipping, could not write %s, %s", failEarlyFile, err.Error())
	}

	loadFailEarlyFile()
	if failStepName != data {
		t.Errorf("did not get correct step name. got %s. expected %s.", failStepName, data)
	}
}

func TestFailEarly(t *testing.T) {
	r := New("test")
	tests := []struct {
		failId  string   // gets saved to `failStepName`
		ybaDev  bool     // if we are in yba-mode = dev
		stepIds []string // Ids to get passed to failEarly
		results []bool   // results[i] is the result of failEarly(stepIds[i])
	}{
		{ // no failing steps
			failId:  "fail.here",
			ybaDev:  true,
			stepIds: []string{"one", "two"},
			results: []bool{false, false},
		},
		{ // last step does not fail, yba_mode is not dev
			failId:  "fail.here",
			ybaDev:  false,
			stepIds: []string{"one", "two", "fail.here"},
			results: []bool{false, false, false},
		},
		{ // last step fails
			failId:  "fail.here",
			ybaDev:  true,
			stepIds: []string{"one", "two", "fail.here"},
			results: []bool{false, false, true},
		},
	}
	for ii, test := range tests {
		t.Run(fmt.Sprintf("%d-%s", ii, t.Name()), func(t *testing.T) {
			failStepName = test.failId
			if test.ybaDev {
				os.Setenv("YBA_MODE", "dev")
			} else {
				os.Setenv("YBA_MODE", "")
			}
			for i := 0; i < len(test.stepIds); i++ {
				result := r.failEarly(test.stepIds[i])
				if result != test.results[i] {
					t.Errorf("unexpected result for id %s. Got %v. Expected %v",
						test.stepIds[i], result, test.results[i])
				}
			}
		})
	}
}

package errorhandler

import (
	"fmt"
	"os"
	"strconv"
	"testing"
)

func TestLoadFailEarlyFile(t *testing.T) {
	data := 12
	bData := []byte(strconv.Itoa(data))
	err := os.WriteFile(failEarlyFile, bData, 0755)
	defer os.Remove(failEarlyFile)
	if err != nil {
		t.Skipf("Skipping, could not write %s, %s", failEarlyFile, err.Error())
	}

	loadFailEarlyFile()
	if failStepCount != data {
		t.Errorf("did not get correct step name. got %d. expected %d.", failStepCount, data)
	}
}

func TestFailEarly(t *testing.T) {
	tests := []struct {
		failId  int    // gets saved to `failStepName`
		ybaDev  bool   // if we are in yba-mode = dev
		results []bool // results[i] is the result of failEarly(stepIds[i])
	}{
		{ // no failing steps
			failId:  7,
			ybaDev:  true,
			results: []bool{false, false},
		},
		{ // last step does not fail, yba_mode is not dev
			failId:  3,
			ybaDev:  false,
			results: []bool{false, false, false},
		},
		{ // last step fails
			failId:  3,
			ybaDev:  true,
			results: []bool{false, false, true},
		},
	}
	for ii, test := range tests {
		t.Run(fmt.Sprintf("%d-%s", ii, t.Name()), func(t *testing.T) {
			// reset the counter as the counter has to be manually controlled by the test.
			counter = 0
			failStepCount = test.failId
			if test.ybaDev {
				os.Setenv("YBA_MODE", "dev")
			} else {
				os.Setenv("YBA_MODE", "")
			}
			for i := 0; i < len(test.results); i++ {
				counter++
				result := failEarly()
				if result != test.results[i] {
					t.Errorf("unexpected result for id %d. Got %v. Expected %v",
						counter, result, test.results[i])
				}
			}
		})
	}
}

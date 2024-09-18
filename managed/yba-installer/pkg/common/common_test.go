package common

import (
	"os"
	"testing"
	"reflect"

	"github.com/spf13/viper"
)

func setAndHandleError(t *testing.T, file, key string, value interface{}) {
	if err := SetYamlValue(file, key, value); err != nil {
		t.Fatalf("error setting yaml value %s: %s", key, err.Error())
	}
}

// TO ENABLE LOGGING IMPORT LOGGING PACKAGE AND RUN LOG.INIT
func TestSetYaml(t *testing.T) {

	filePath := "/tmp/test_common.yml"

	yamlStr := ""
	_ = os.Remove(filePath)
	err := os.WriteFile(filePath, []byte(yamlStr), 0600)
	if err != nil {
		t.Fatalf("error writing file %s: %s", filePath, err)
	}

	setAndHandleError(t, filePath, "foo.bar.list", []string{"abcd", "efgh"})
	setAndHandleError(t, filePath, "level1", "new1")
	setAndHandleError(t, filePath, "foo.bar.abc", "new2")
	setAndHandleError(t, filePath, "foo.bar.ghi", "new3")
	setAndHandleError(t, filePath, "biz.baz.booz", "new4")
	setAndHandleError(t, filePath, "level2", "new5")
	setAndHandleError(t, filePath, "foo.bar.list2", []string{})

	v := viper.New()
	v.SetConfigFile(filePath)
	err = v.ReadInConfig()
	if err != nil {
		t.Fatalf("error reading yaml %s %s", filePath, err)
	}

	real := v.GetString("foo.bar.abc")
	expected := "new2"
	if real != expected {
		t.Fatalf("yaml entry doesn't match expected '%s' '%s'", real, expected)
	}

	real = v.GetString("level1")
	expected = "new1"
	if real != expected {
		t.Fatalf("yaml entry doesn't match expected '%s' '%s'", real, expected)
	}

	real = v.GetString("foo.bar.ghi")
	expected = "new3"
	if real != expected {
		t.Fatalf("yaml entry doesn't match expected '%s' '%s'", real, expected)
	}

	real = v.GetString("biz.baz.booz")
	expected = "new4"
	if real != expected {
		t.Fatalf("yaml entry doesn't match expected '%s' '%s'", real, expected)
	}

	real = v.GetString("level2")
	expected = "new5"
	if real != expected {
		t.Fatalf("yaml entry doesn't match expected '%s' '%s'", real, expected)
	}

	list := v.GetStringSlice("foo.bar.list")
    expectedList := []string{"abcd", "efgh"}

    if !reflect.DeepEqual(list, expectedList) {
        t.Fatalf("yaml entry doesn't match expected %#v %#v", list, expectedList)
    }

	list = v.GetStringSlice("foo.bar.list2")
	if list == nil {
		list = []string{}
	}
    expectedList = []string{}

    if !reflect.DeepEqual(list, expectedList) {
        t.Fatalf("yaml entry doesn't match expected %#v %#v", list, expectedList)
    }

}

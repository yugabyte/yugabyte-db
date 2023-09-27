package common

import (
	"os"
	"testing"

	"github.com/spf13/viper"
)

func TestSetYaml(t *testing.T) {

	filePath := "/tmp/test_common.yml"

	yamlStr := `
# test comment
foo: # more test
   bar:
      abc: ""
      def: etc

level1: etc
`
	_ = os.Remove(filePath)
	err := os.WriteFile(filePath, []byte(yamlStr), 0600)
	if err != nil {
		t.Fatalf("error writing file %s: %s", filePath, err)
	}

	SetYamlValue(filePath, "level1", "new1")
	SetYamlValue(filePath, "foo.bar.abc", "new2")
	SetYamlValue(filePath, "foo.bar.ghi", "new3")
	SetYamlValue(filePath, "biz.baz.booz", "new4")
	SetYamlValue(filePath, "level2", "new5")

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
}

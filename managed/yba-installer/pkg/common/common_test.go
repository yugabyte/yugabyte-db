package common

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"

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

// TestSetYaml covers building a config up from an empty file. A migration instead adds a new
// top-level block to a config that already has content, which takes a different path through
// setYamlValue, and must not disturb what is already there.
func TestSetYamlValueAddsBlockToPopulatedConfig(t *testing.T) {
	const existing = `installRoot: "/opt/yugabyte"
host: ""
service_username: "yugabyte"
platform:
   port: 443
   keyStorePassword: "existing-password"
`
	filePath := filepath.Join(t.TempDir(), "yba-ctl.yml")
	if err := os.WriteFile(filePath, []byte(existing), 0600); err != nil {
		t.Fatalf("error writing file %s: %s", filePath, err)
	}

	if err := SetYamlValue(filePath, "fips.enabled", false); err != nil {
		t.Fatalf("error setting fips.enabled: %s", err)
	}

	v := viper.New()
	v.SetConfigFile(filePath)
	if err := v.ReadInConfig(); err != nil {
		t.Fatalf("error reading yaml %s: %s", filePath, err)
	}
	if !v.IsSet("fips.enabled") {
		t.Fatalf("fips.enabled was not added to %s", filePath)
	}
	if v.GetBool("fips.enabled") {
		t.Fatalf("fips.enabled should be false")
	}
	if real := v.GetString("platform.keyStorePassword"); real != "existing-password" {
		t.Fatalf("existing entry was overwritten: got '%s'", real)
	}
	if real := v.GetInt("platform.port"); real != 443 {
		t.Fatalf("existing entry was overwritten: got '%d'", real)
	}
}

// An existing password must come back untouched: rotating it under a running install would leave
// the keystore on disk unopenable by the service that is already using it.
func TestEnsureGeneratedPasswordKeepsAnExistingValue(t *testing.T) {
	viper.Set("perfAdvisor.tls.keystorePassword", "already-set-password")
	t.Cleanup(func() { viper.Set("perfAdvisor.tls.keystorePassword", "") })

	got, err := EnsureGeneratedPassword("perfAdvisor.tls.keystorePassword")
	if err != nil {
		t.Fatalf("EnsureGeneratedPassword: %v", err)
	}
	if got != "already-set-password" {
		t.Fatalf("got %q, want the stored value", got)
	}
}

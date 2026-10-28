package checks

import (
	"reflect"
	"testing"

	"github.com/spf13/viper"
)

func TestShortFipsPasswords(t *testing.T) {
	t.Cleanup(viper.Reset)
	viper.Set("postgres.install.enabled", true)
	viper.Set("prometheus.enableAuth", true)
	viper.Set("platform.keyStorePassword", "short")
	viper.Set("postgres.install.password", "also-short")
	viper.Set("prometheus.authPassword", "")            // empty: generated later
	viper.Set("postgres.useExisting.password", "short") // not in use
	viper.Set("nodeExporter.authPassword", "fourteen-chars")

	viper.Set("fips.enabled", false)
	if short := shortFipsPasswords(); len(short) != 0 {
		t.Fatalf("checked passwords outside FIPS mode: %v", short)
	}

	viper.Set("fips.enabled", true)
	want := []string{"platform.keyStorePassword", "postgres.install.password"}
	if short := shortFipsPasswords(); !reflect.DeepEqual(short, want) {
		t.Fatalf("shortFipsPasswords() = %v, want %v", short, want)
	}

	if res := FipsPasswords.Execute(); res.Status != StatusCritical || res.Error == nil {
		t.Fatalf("Execute() = %v, want critical", res)
	}
	if FipsPasswords.SkipAllowed() {
		t.Fatal("fips-passwords must not be skippable")
	}
}

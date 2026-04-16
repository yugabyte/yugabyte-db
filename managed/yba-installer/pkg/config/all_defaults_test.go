package config

import (
	"reflect"
	"strings"
	"testing"

	"github.com/spf13/viper"
)

// getViperKeyPaths recursively collects all viper key paths for a struct
func getViperKeyPaths(prefix string, typ reflect.Type, t *testing.T) []string {
	var keys []string
	for i := 0; i < typ.NumField(); i++ {
		field := typ.Field(i)
		tag := field.Tag.Get("mapstructure")
		if tag == "-" {
			continue
		}
		squash, tagKey := parseTag(tag, field.Name, t)
		var key string
		if squash {
			if field.Type.Kind() != reflect.Struct {
				t.Errorf("',squash' used on non-struct field '%s'", field.Name)
			}
			key = prefix // squash: do not add field name
			// Only recurse, do not add a key for the squashed field itself
			keys = append(keys, getViperKeyPaths(key, field.Type, t)...)
			continue
		} else {
			key = tagKey
			if prefix != "" {
				key = prefix + "." + tagKey
			}
		}
		ft := field.Type
		// Skip time.Time (and similar) to avoid recursing into standard library structs
		if ft.Kind() == reflect.Struct && ft.Name() != "Time" {
			keys = append(keys, getViperKeyPaths(key, ft, t)...)
		} else {
			keys = append(keys, key)
		}
	}
	return keys
}

// parseTag determines the config key name for a struct field based on its mapstructure tag.
// Returns (squash, key):
//   - If the tag is ",squash", returns (true, "").
//   - If the tag contains a comma (e.g. "foo_bar,squash"), only the part before the comma is used
//     as the key.
//   - Otherwise, the tag value is used as the key.
//   - It is invalid for a field to have no tag or an empty tag (except for ',squash').
//     This enforces that all config fields must have a mapstructure tag or be explicitly squashed.
func parseTag(tag, fieldName string, t *testing.T) (bool, string) {
	if tag == "" {
		t.Errorf("All config struct fields must have a mapstructure tag or ',squash': missing for "+
			"field '%s'", fieldName)
	}
	if tag == ",squash" {
		return true, ""
	}
	commaIdx := len(tag)
	if idx := strings.Index(tag, ","); idx >= 0 {
		commaIdx = idx
	}
	tval := tag[:commaIdx]
	if tval == "" {
		t.Errorf("Invalid mapstructure tag for field '%s': tag is just a comma", fieldName)
	}
	return false, tval
}

func TestAllConfigFieldsHaveDefaults(t *testing.T) {
	v := viper.New()
	addDefaults(v)
	var missing []string
	rcType := reflect.TypeOf(rootConfig{})
	keys := getViperKeyPaths("", rcType, t)
	for _, key := range keys {
		if !v.IsSet(key) {
			missing = append(missing, key)
		}
	}
	if len(missing) > 0 {
		t.Errorf("Missing default values for config keys: %v", missing)
	}
}

// Copyright (c) YugabyteDB, Inc.

package config

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	md "node-agent/app/task/module"
	"node-agent/util"
	"os"
	"path/filepath"
	"reflect"
	"strconv"
	"strings"
	"time"

	"gopkg.in/ini.v1"
)

const (
	DefaultINISection = ini.DefaultSection
)

type Args struct {
	Command         string
	YnpBasePath     string
	SpecificModules []string
	SkipModules     []string
	ConfigFile      string
	PreflightCheck  bool
	YnpConfig       map[string]map[string]any
	DryRun          bool
	ListModules     bool
	NoRoot          bool
	Root            bool
}

// Module represents a YNP module.
type Module interface {
	BasePath() string // Base path of the module.
	Name() string     // Name of the module.
	RenderTemplates(ctx context.Context, values map[string]any) (*RenderedTemplates, error)
}

// BaseModule provides common functionality for all modules.
type BaseModule struct {
	basePath string // Path to the module base dir.
	name     string // Name of the module.
}

// INIConfig represents the parsed INI configuration.
type INIConfig struct {
	// Ordered sections.
	sections []string
	// Section name to key-value mapping.
	values map[string]map[string]any
}

// Sections returns the list of section names in the defined order.
func (c *INIConfig) Sections() []string {
	return c.sections
}

// SectionValue returns the key-value mapping for the given section.
func (c *INIConfig) SectionValue(section string) map[string]any {
	return c.values[section]
}

// SectionValues returns the mapping of all sections to their key-value mappings.
// This does not gurantee order of sections.
func (c *INIConfig) SectionValues() map[string]map[string]any {
	return c.values
}

// DefaultSectionValue returns the key-value mapping for the default section.
func (c *INIConfig) DefaultSectionValue() map[string]any {
	return c.values[DefaultINISection]
}

// RenderedTemplates holds the rendered templates for a module.
type RenderedTemplates struct {
	name     string            // Module name.
	rendered map[string]string // phase to rendered content.
}

// Name returns the module name.
func (r *RenderedTemplates) Name() string {
	return r.name
}

// RenderedContent returns the rendered content for the given phase.
func (r *RenderedTemplates) RenderedContent(phase string) string {
	if content, ok := r.rendered[phase]; ok {
		return content
	}
	return ""
}

// RenderedPhases returns the list of phases for which templates were rendered.
func (r *RenderedTemplates) RenderedPhases() []string {
	phases := make([]string, 0, len(r.rendered))
	for phase := range r.rendered {
		phases = append(phases, phase)
	}
	return phases
}

// CommandFactory is a factory function to create a Command.
type CommandFactory func(context.Context, *INIConfig, Args) Command

// Command represents a command to be executed.
type Command interface {
	Init() error
	Validate() error
	DryRun() error
	RunPreflightChecks() error
	ListModules() error
	Execute() error
	Cleanup()
}

func NewBaseModule(name, basePath string) *BaseModule {
	return &BaseModule{
		basePath: basePath,
		name:     name, // Name of the module for resources e.g jinja files folder.
	}
}

func (bm *BaseModule) RenderTemplates(
	ctx context.Context,
	values map[string]any,
) (*RenderedTemplates, error) {
	// Template name to file mapping.
	templates := map[string]string{
		"run":      "run.j2",
		"precheck": "precheck.j2",
	}
	output := &RenderedTemplates{name: bm.Name(), rendered: make(map[string]string)}
	for key, templateFile := range templates {
		templatePath := filepath.Join(bm.BasePath(), "templates", templateFile)
		if _, err := os.Stat(templatePath); os.IsNotExist(err) {
			continue
		}
		rendered, err := md.ResolveTemplate(ctx, values, templatePath)
		if err != nil {
			err = fmt.Errorf("failed to render template %s: %w", templatePath, err)
			return nil, err
		}
		output.rendered[key] = rendered
	}
	return output, nil
}

func (bm *BaseModule) BasePath() string {
	return bm.basePath
}

func (bm *BaseModule) Name() string {
	return bm.name
}

func (bm *BaseModule) String() string {
	return bm.name + "@" + bm.basePath
}

func LookupType[T any](
	m map[string]any,
	key string,
	defaultVal T,
	transformer func(string) (T, error),
) T {
	val, ok := m[key]
	if !ok {
		return defaultVal
	}
	typedVal, ok := val.(T)
	if ok {
		return typedVal
	}
	strVal := fmt.Sprintf("%v", val)
	result, err := transformer(strVal)
	if err == nil {
		return result
	}
	return defaultVal
}

func GetBool(m map[string]any, key string, defaultVal bool) bool {
	return LookupType(m, key, defaultVal, func(s string) (bool, error) {
		return strconv.ParseBool(s)
	})
}

func GetInt(m map[string]any, key string, defaultVal int64) int64 {
	return LookupType(m, key, defaultVal, func(s string) (int64, error) {
		return strconv.ParseInt(s, 10, 64)
	})
}

func GetFloat(m map[string]any, key string, defaultVal float64) float64 {
	return LookupType(m, key, defaultVal, func(s string) (float64, error) {
		return strconv.ParseFloat(s, 64)
	})
}

func processNestedConfigs(
	iniConfig *INIConfig,
) (*INIConfig, error) {
	out := &INIConfig{
		sections: make([]string, 0),
		values:   make(map[string]map[string]any),
	}
	for _, sectionKey := range iniConfig.sections {
		sectionValues := iniConfig.values[sectionKey]
		if sectionKey == DefaultINISection {
			out.sections = append(out.sections, sectionKey)
			out.values[sectionKey] = sectionValues
			continue
		}
		if !strings.Contains(sectionKey, ".") {
			for defaultKey, defaultValue := range iniConfig.values[DefaultINISection] {
				if _, exists := sectionValues[defaultKey]; !exists {
					sectionValues[defaultKey] = defaultValue
				}
			}
			for k, v := range sectionValues {
				if out.values[sectionKey] == nil {
					out.values[sectionKey] = make(map[string]any)
				}
				out.values[sectionKey][k] = v
			}
			out.sections = append(out.sections, sectionKey)
			continue
		}
		keyList := strings.Split(sectionKey, ".")
		sectionKey = keyList[0]
		nested, exists := out.values[sectionKey]
		if !exists {
			nested = make(map[string]any)
			out.sections = append(out.sections, sectionKey)
			out.values[sectionKey] = nested
		}
		// First key is section name, last key is where to put the values.
		for i := 1; i < len(keyList)-1; i++ {
			key := keyList[i]
			if _, exists := nested[key]; !exists {
				nested[key] = make(map[string]any)
			}
			nested = nested[key].(map[string]any)
		}
		defaultKeys, ok := iniConfig.values[DefaultINISection]
		if !ok {
			defaultKeys = map[string]any{}
		}
		newSectionValues := make(map[string]any)
		for key, value := range sectionValues {
			// Skip default keys for the nested objects.
			if _, ok := defaultKeys[key]; !ok {
				newSectionValues[key] = value
			}
		}
		nested[keyList[len(keyList)-1]] = newSectionValues
	}
	return out, nil
}

// GenerateConfigINI generates the INI configuration file based on the provided arguments.
// Note: Booleans are printed as True or False string.
func GenerateConfigINI(
	ctx context.Context,
	args Args,
) (*INIConfig, error) {
	configTemplate := filepath.Join(args.YnpBasePath, "configs/config.j2")
	configPath := filepath.Join(args.YnpBasePath, "configs/config.ini")
	ynpValues := map[string]any{
		"ynp_config":     args.YnpConfig,
		"ynp_dir":        args.YnpBasePath,
		"is_noroot_mode": args.NoRoot,
		"is_root_mode":   args.Root,
		"start_time":     time.Now().Unix(),
		"ynp_driver":     "golang",
	}
	jsonConfig, _ := json.MarshalIndent(ynpValues, "", "  ")
	util.FileLogger().Infof(ctx, "YNP config here: %s", string(jsonConfig))
	renderedConfig, err := md.CopyFile(ctx, ynpValues, configTemplate, configPath, 0644, "")
	if err != nil {
		return nil, err
	}
	iniConfig, err := ini.Load([]byte(renderedConfig))
	if err != nil {
		return nil, fmt.Errorf("Failed to load INI content: %w", err)
	}
	configOutput := &INIConfig{
		sections: iniConfig.SectionStrings(),
		values:   make(map[string]map[string]any),
	}
	for _, section := range iniConfig.Sections() {
		sectionMap := make(map[string]any)
		for _, key := range section.Keys() {
			sectionMap[key.Name()] = key.Value()
		}
		configOutput.values[section.Name()] = sectionMap
	}
	configOutput.values = FixParsedConfigMap(configOutput.values)
	return processNestedConfigs(configOutput)
}

// FixParsedConfigMap fixes the types in the parsed config map.
// Keys are converted to strings, and boolean-like strings are converted to bools.
func FixParsedConfigMap(input map[string]map[string]any) map[string]map[string]any {
	fixed := make(map[string]map[string]any)
	for k, v := range input {
		fixed[k] = fixParsedConfig(v).(map[string]any)
	}
	return fixed
}

func fixParsedConfig(input any) any {
	tp := reflect.TypeOf(input)
	val := reflect.ValueOf(input)
	if tp.Kind() == reflect.Map {
		var fixedMap = make(map[string]any)
		for _, key := range val.MapKeys() {
			if reflect.TypeOf(key).Kind() == reflect.String {
				fixedMap[key.String()] = fixParsedConfig(val.MapIndex(key).Interface())
			} else {
				strKey := fmt.Sprintf("%v", key.Interface())
				fixedMap[strKey] = fixParsedConfig(val.MapIndex(key).Interface())
			}
		}
		return fixedMap
	}
	if tp.Kind() == reflect.Slice {
		var fixedSlice []any
		for i := 0; i < val.Len(); i++ {
			fixedSlice = append(fixedSlice, fixParsedConfig(val.Index(i).Interface()))
		}
		return fixedSlice
	}
	if tp.Kind() == reflect.Float32 || tp.Kind() == reflect.Float64 {
		// Correction for integers parsed as floats. An alternative is to use string for numbers.
		fVal := val.Float()
		truncated := math.Trunc(fVal)
		if truncated == fVal {
			return int(fVal)
		}
		return fVal
	}
	return input
}

// Copyright (c) YugaByte, Inc.

package util

import (
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"reflect"
	"sync"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
	instance      Config
	syncMap       *sync.Map
	mutex         *sync.Mutex
	currentConfig = DefaultConfig
)

type Config struct {
	rwLock        *sync.RWMutex
	viperInstance *viper.Viper
}

func init() {
	syncMap = &sync.Map{}
	mutex = &sync.Mutex{}
}

func SetCurrentConfig(config string) {
	mutex.Lock()
	defer mutex.Unlock()
	currentConfig = config
}

func CurrentConfig() *Config {
	config, err := ConfigWithName(currentConfig)
	if err != nil {
		panic(err)
	}
	return config
}

// Returns the config by name.
func ConfigWithName(name string) (*Config, error) {
	i, ok := syncMap.Load(name)
	if ok {
		return i.(*Config), nil
	}
	mutex.Lock()
	defer mutex.Unlock()
	config := &Config{rwLock: &sync.RWMutex{}}
	configDir := ConfigDir()
	// Create config directory if not exists.
	err := os.MkdirAll(configDir, os.ModePerm)
	if err != nil {
		fmt.Errorf("Error while creating config - %s", err.Error())
		return nil, err
	}
	// Create config file if not exists.
	filename := fmt.Sprintf("%s/%s.yml", configDir, name)
	if _, err = os.Stat(filename); os.IsNotExist(err) {
		file, err := os.Create(filename)
		if err != nil {
			fmt.Errorf("Error while creating config file - %s", err.Error())
			return nil, err
		}
		file.Close()
	} else if err != nil {
		fmt.Errorf("Error while creating config - %s", err.Error())
		return nil, err
	}
	config.viperInstance = viper.New()
	config.viperInstance.SetConfigType("yml")
	config.viperInstance.AddConfigPath(configDir)
	config.viperInstance.SetConfigName(name)
	err = config.viperInstance.ReadInConfig()
	if err != nil {
		fmt.Errorf("Error reading the config file - %s", err.Error())
		return nil, err
	}
	syncMap.Store(name, config)
	return config, nil
}

// Checks if a key is present in the config.
func (config *Config) Present(key string) bool {
	config.rwLock.RLock()
	defer config.rwLock.Unlock()
	if config.viperInstance.Get(key) != nil {
		return true
	}
	return false
}

// Returns the value against a key in the config.
// Returns empty string if the key is absent in the config.
func (config *Config) String(key string) string {
	config.rwLock.RLock()
	defer config.rwLock.RUnlock()
	return config.viperInstance.GetString(key)
}

// Returns the value against a key in the config.
func (config *Config) Bool(key string) bool {
	config.rwLock.RLock()
	defer config.rwLock.RUnlock()
	return config.viperInstance.GetBool(key)
}

// Returns the value against a key in the config.
func (config *Config) Float(key string) float64 {
	config.rwLock.RLock()
	defer config.rwLock.RUnlock()
	return config.viperInstance.GetFloat64(key)
}

// Returns the int value against a key in the config.
// Returns 0 if the key is absent in the config.
func (config *Config) Int(key string) int {
	config.rwLock.RLock()
	defer config.rwLock.RUnlock()
	return config.viperInstance.GetInt(key)
}

// Creates or updates a value for a key in the config.
func (config *Config) Update(key string, val any) error {
	config.rwLock.Lock()
	defer config.rwLock.Unlock()
	config.viperInstance.Set(key, val)
	return config.viperInstance.WriteConfig()
}

// Creates or updates a value for a key in the config.
func (config *Config) CompareAndUpdate(key string, expected, val any) (bool, error) {
	var err error
	config.rwLock.Lock()
	defer config.rwLock.Unlock()
	if reflect.DeepEqual(config.viperInstance.Get(key), expected) {
		config.viperInstance.Set(key, val)
		err = config.viperInstance.WriteConfig()
		if err == nil {
			return true, nil
		}
	}
	return false, err
}

// Removes the given from the config file.
func (config *Config) Remove(key string) error {
	config.rwLock.Lock()
	defer config.rwLock.Unlock()
	config.viperInstance.Set(key, "")
	return config.viperInstance.WriteConfig()
}

func (config *Config) StoreCommandFlagBool(
	ctx context.Context,
	cmd *cobra.Command,
	flagName, configKey string) (bool, error) {
	isPassed := cmd.Flags().Changed(flagName)
	if isPassed {
		value, err := cmd.Flags().GetBool(flagName)
		if err != nil {
			FileLogger().Errorf(ctx, "Unable to get %s - %s", flagName, err.Error())
			return value, err
		}
		err = config.Update(configKey, value)
		if err != nil {
			FileLogger().Errorf(ctx, "Unable to save %s - %s", configKey, err.Error())
			return value, err
		}
		return value, nil
	}
	return config.Bool(configKey), nil
}

func (config *Config) StoreCommandFlagString(
	ctx context.Context,
	cmd *cobra.Command,
	flagName, configKey string,
	defaultValue *string,
	isRequired bool,
	validator func(string) (string, error),
) (string, error) {
	value, err := cmd.Flags().GetString(flagName)
	if err != nil {
		FileLogger().Errorf(ctx, "Unable to get %s - %s", flagName, err.Error())
		return value, err
	}
	if value == "" {
		if defaultValue != nil {
			// Save the default value.
			err = config.Update(configKey, *defaultValue)
			if err != nil {
				FileLogger().Errorf(ctx, "Unable to save %s - %s", configKey, err.Error())
				return value, err
			}
			value = *defaultValue
		}
	} else {
		if validator != nil {
			value, err = validator(value)
			if err != nil {
				FileLogger().Errorf(ctx, "Error in validating value for %s - %s", flagName, err.Error())
				return value, err
			}
		}
		err = config.Update(configKey, value)
		if err != nil {
			FileLogger().Errorf(ctx, "Unable to save %s - %s", configKey, err.Error())
			return value, err
		}
		return value, nil
	}
	if isRequired {
		value = config.String(configKey)
		if value == "" {
			err = fmt.Errorf("Unable to get %s from config", configKey)
			FileLogger().Error(ctx, err.Error())
			return value, err
		}
	}
	return value, nil
}

func MustVersion() string {
	version, err := Version()
	if err != nil {
		FileLogger().Fatalf(nil, "Error in getting version - %s", err.Error())
	}
	return version
}

func Version() (string, error) {
	var version string
	content, err := ioutil.ReadFile(VersionFile())
	if err != nil {
		return version, fmt.Errorf("Error when opening file - %s", err.Error())
	}
	data := struct {
		Version  string `json:"version_number"`
		BuildNum string `json:"build_number"`
	}{}
	err = json.Unmarshal(content, &data)
	if err != nil {
		return version, fmt.Errorf("Error in parsing verson file - %s", err.Error())
	}
	format := "%s-%s"
	if IsDigits(data.BuildNum) {
		format = "%s-b%s"
	}
	version = fmt.Sprintf(format, data.Version, data.BuildNum)
	return version, nil
}

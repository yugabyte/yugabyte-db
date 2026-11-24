// Copyright (c) YugabyteDB, Inc.

package util

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path"
	"reflect"
	"sync"
	"syscall"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var (
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
	configPath := path.Join(ConfigDir(), currentConfig)
	config, err := ConfigWithName(configPath)
	if err != nil {
		panic(err)
	}
	return config
}

// Returns the config by name.
func ConfigWithName(configPath string) (*Config, error) {
	configPath = path.Clean(configPath)
	i, ok := syncMap.Load(configPath)
	if ok {
		return i.(*Config), nil
	}
	mutex.Lock()
	defer mutex.Unlock()
	configDir := path.Dir(configPath)
	configName := path.Base(configPath)
	config := &Config{rwLock: &sync.RWMutex{}}
	// Create config directory if not exists.
	err := os.MkdirAll(configDir, os.ModePerm)
	if err != nil {
		err = fmt.Errorf("Error while creating config - %s", err.Error())
		return nil, err
	}
	// Create config file if not exists.
	filename := fmt.Sprintf("%s.yml", configPath)
	if _, err = os.Stat(filename); os.IsNotExist(err) {
		file, err := os.Create(filename)
		if err != nil {
			err = fmt.Errorf("Error while creating config file - %s", err.Error())
			return nil, err
		}
		file.Close()
	} else if err != nil {
		err = fmt.Errorf("Error while creating config - %s", err.Error())
		return nil, err
	}
	config.viperInstance = viper.New()
	config.viperInstance.SetConfigType("yml")
	config.viperInstance.AddConfigPath(configDir)
	config.viperInstance.SetConfigName(configName)
	err = config.viperInstance.ReadInConfig()
	if err != nil {
		err = fmt.Errorf("Error reading the config file - %s", err.Error())
		return nil, err
	}
	syncMap.Store(configPath, config)
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
	_, err := config.mutate(func(instance *viper.Viper) bool {
		instance.Set(key, val)
		return true
	})
	return err
}

// Creates or updates a value for a key in the config.
func (config *Config) CompareAndUpdate(key string, expected, val any) (bool, error) {
	return config.mutate(func(instance *viper.Viper) bool {
		if !reflect.DeepEqual(instance.Get(key), expected) {
			return false
		}
		instance.Set(key, val)
		return true
	})
}

// Reload re-reads the config file, dropping what this process loaded or set earlier.
func (config *Config) Reload() error {
	config.rwLock.Lock()
	defer config.rwLock.Unlock()
	return config.withFileLock(syscall.LOCK_SH, func() error {
		instance, err := config.readFile()
		if err == nil {
			config.viperInstance = instance
		}
		return err
	})
}

// Removes the given from the config file.
func (config *Config) Remove(key string) error {
	_, err := config.mutate(func(instance *viper.Viper) bool {
		instance.Set(key, "")
		return true
	})
	return err
}

// mutate applies change to the config as it is on disk now, not to what this process loaded, and
// writes it if change returns true. Several node agent processes (the server, CLI commands, a
// duplicate server) share the file; writing back an older snapshot would undo their writes, e.g.
// the cert switch of an upgrade (PLAT-24106).
func (config *Config) mutate(change func(*viper.Viper) bool) (bool, error) {
	config.rwLock.Lock()
	defer config.rwLock.Unlock()
	changed := false
	err := config.withFileLock(syscall.LOCK_EX, func() error {
		instance, err := config.readFile()
		if err != nil {
			return err
		}
		config.viperInstance = instance
		if changed = change(instance); changed {
			return instance.WriteConfig()
		}
		return nil
	})
	return changed && err == nil, err
}

// readFile reads the config file into a fresh viper. ReadInConfig on the existing one is not
// enough: values from Set live in viper's override layer, which shadows the file.
func (config *Config) readFile() (*viper.Viper, error) {
	instance := viper.New()
	instance.SetConfigType("yml")
	instance.SetConfigFile(config.viperInstance.ConfigFileUsed())
	if err := instance.ReadInConfig(); err != nil {
		return nil, fmt.Errorf("Error reading the config file - %s", err.Error())
	}
	return instance, nil
}

// withFileLock runs fn holding a flock on a lock file next to the config file. WriteConfig
// truncates before writing, so readers take it too to never see a partial file.
func (config *Config) withFileLock(how int, fn func() error) error {
	lockPath := config.viperInstance.ConfigFileUsed() + ".lock"
	file, err := os.OpenFile(lockPath, os.O_RDWR|os.O_CREATE, 0644)
	if err != nil {
		return err
	}
	defer file.Close()
	if err := syscall.Flock(int(file.Fd()), how); err != nil {
		return err
	}
	return fn()
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

// Dump dumps the config values into a JSON string.
func (config *Config) Dump(ctx context.Context) (string, error) {
	if config == nil || config.viperInstance == nil {
		return "", nil
	}
	ba, err := json.Marshal(config.viperInstance.AllSettings())
	if err != nil {
		FileLogger().Errorf(ctx, "Error in dumping config %s", err.Error())
		return "", err
	}
	return string(ba), nil
}

func MustVersion() string {
	version, err := Version()
	if err != nil {
		FileLogger().Fatalf(context.TODO(), "Error in getting version - %s", err.Error())
	}
	return version
}

func Version() (string, error) {
	var version string
	content, err := os.ReadFile(VersionFile())
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

/*
 * Copyright (c) YugaByte, Inc.
 */
package util

import (
	"fmt"
	"os"
	"sync"

	"github.com/spf13/viper"
)

var (
	instance Config
	once     sync.Once
)

type Config struct {
	rwLock        *sync.RWMutex
	viperInstance *viper.Viper
}

var (
	//Platform Config keys.
	PlatformHost          = "platform.host"
	CustomerId            = "platform.cuuid"
	UserId                = "platform.userId"
	PlatformPort          = "platform.port"
	ProviderId            = "platform.puuid"
	PlatformCerts         = "platform.certs"
	PlatformCertsUpgrade  = "platform.upgrade_certs"
	PlatformVersion       = "platform.version"
	PlatformVersionUpdate = "platform.update_version"

	//Node Config keys.
	NodeIP           = "node.ip"
	RequestTimeout   = "node.request_timeout_sec"
	NodeName         = "node.name"
	NodeAgentId      = "node.agent.uuid"
	NodeId           = "node.uuid"
	NodeInstanceType = "node.instance_type"
	NodeAzId         = "node.azid"
	NodeRegion       = "node.region"
	NodeZone         = "node.zone"
	NodeInstanceName = NodeInstanceType
	NodePingInterval = "node.ping_interval_sec"
	NodeLogger       = "node.log"

	//Certs.
	AgentCertFile = "node_agent.crt"
	AgentKeyFile  = "node_agent.key"
)

//Returns the config instance. Returns a Nil Pointer
//if the config has not been initialized.
func GetConfig() *Config {
	return &instance
}

func InitConfig(configName string) error {
	var err error
	once.Do(func() {
		instance = Config{rwLock: &sync.RWMutex{}, viperInstance: viper.New()}
		configDir := GetConfigDir()
		//Create Config Directory if not exists
		err = os.MkdirAll(configDir, os.ModePerm)
		if err != nil {
			cliLogger.Errorf("Error while creating config: %s", err.Error())
			return
		}
		//Create config file if not exists
		configFileName := fmt.Sprintf("%s/%s.yml", configDir, configName)
		if _, err = os.Stat(configFileName); os.IsNotExist(err) {
			_, err = os.Create(configFileName)
			if err != nil {
				cliLogger.Errorf("Error while creating config file: %s", err.Error())
				return
			}
		} else if err != nil {
			cliLogger.Errorf("Error while creating config: %s", err.Error())
			return
		}

		instance.viperInstance.SetConfigType("yml")
		instance.viperInstance.AddConfigPath(configDir)
		//Pickup test config when the env is set to test,
		//otherwise pickup config.yml.
		instance.viperInstance.SetConfigName(configName)
		err = instance.viperInstance.ReadInConfig()
		if err != nil {
			fileLogger.Errorf("Error reading the config file, %s", err)
		}
	})

	if err != nil {
		return err
	}
	return nil
}

//Checks if a key is present in the config.
func (i *Config) Present(key string) bool {
	i.rwLock.RLock()
	defer i.rwLock.Unlock()
	if i.viperInstance.Get(key) != nil {
		return true
	}
	return false
}

//Returns the value against a key in the config.
//Returns empty string if the key is absent in the config.
func (i *Config) GetString(key string) string {
	//Put a Read Lock when trying to access a key
	//in order to prevent a concurrent write to the config file
	i.rwLock.RLock()
	defer i.rwLock.RUnlock()
	return i.viperInstance.GetString(key)
}

//Returns the value against a key in the config.
//Returns empty string if the key is absent in the config.
func (i *Config) GetBool(key string) bool {
	//Put a Read Lock when trying to access a key
	//in order to prevent a concurrent write to the config file.
	i.rwLock.RLock()
	defer i.rwLock.RUnlock()
	return i.viperInstance.GetBool(key)
}

//Returns the value against a key in the config.
//Returns empty string if the key is absent in the config.
func (i *Config) GetFloat(key string) float64 {
	//Put a Read Lock when trying to access a key
	//in order to prevent a concurrent write to the config file.
	i.rwLock.RLock()
	defer i.rwLock.RUnlock()
	return i.viperInstance.GetFloat64(key)
}

//Returns a int value against a key in the config.
//Returns 0 if the key is absent in the config.
func (i *Config) GetInt(key string) int {
	i.rwLock.RLock()
	defer i.rwLock.RUnlock()

	//Return 0 if the key is not present in the config.
	return i.viperInstance.GetInt(key)
}

//Creates/Updates a value for a key in the config
//Panic if the write fails.
func (i *Config) Update(key string, val string) {
	//While writing the configs to the file, create
	//a write lock and block the goroutines to
	//read the config until the write is done.
	i.rwLock.Lock()
	defer i.rwLock.Unlock()
	i.viperInstance.Set(key, val)
	err := i.viperInstance.WriteConfig()
	if err != nil {
		panic(err)
	}
}

//Removes the given from the config file.
//Puts empty string if the key doesn't exist.
//TODO: Remove the transient key instead of placing empty value.
func (i *Config) Remove(key string) {
	i.Update(key, "")
}

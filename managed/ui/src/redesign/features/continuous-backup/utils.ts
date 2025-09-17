import { CustomerConfig, CustomerConfigType, StorageConfig } from '../../../components/backupv2';

// Classify the customer config so typescript can infer the correct type.
export const isStorageConfig = (
  customerConfig: CustomerConfig
): customerConfig is StorageConfig => {
  return customerConfig.type === CustomerConfigType.STORAGE;
};

// This type assertion can be removed after updating to Typescript 5.5 as the
// type checker will be able to infer the correct type post version 5.5:
// https://devblogs.microsoft.com/typescript/announcing-typescript-5-5/#inferred-type-predicates
export const getStorageConfigs = (customerConfigs: CustomerConfig[]): StorageConfig[] =>
  customerConfigs.filter((customerConfig) => isStorageConfig(customerConfig)) as StorageConfig[];

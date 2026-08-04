import { RunTimeConfigEntry } from '@app/redesign/features/universe/universe-form/utils/dto';

export const getDefaultK8NodeSpec = (runtimeConfigs: any) => {
  const memorySize = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_memory_size_gb'
  )?.value;

  const CPUCores = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.default_cpu_cores'
  )?.value;

  return {
    memorySize,
    CPUCores
  };
};

export const getK8MemorySizeRange = (runtimeConfigs: any) => {
  const minMemorySize = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.min_memory_size_gb'
  )?.value;

  const maxMemorySize = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.max_memory_size_gb'
  )?.value;

  return {
    minMemorySize,
    maxMemorySize
  };
};

export const getK8CPUCoresRange = (runtimeConfigs: any) => {
  const minCPUCores = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.min_cpu_cores'
  )?.value as number;

  const maxCPUCores = runtimeConfigs?.configEntries?.find(
    (c: RunTimeConfigEntry) => c.key === 'yb.kubernetes.max_cpu_cores'
  )?.value as number;

  return {
    minCPUCores,
    maxCPUCores
  };
};

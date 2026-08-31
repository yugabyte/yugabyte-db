// Copyright (c) YugabyteDB, Inc.

import { RuntimeConfigKey } from './constants';

export type RuntimeConfigEntry = {
  key: string;
  value: string;
};

/**
 * True when the Kubernetes operator owns the resource and the runtime flag
 * `yb.kubernetes.operator.block_api_operator_owned_resources` is enabled.
 */
export const isK8OperatorApiBlocked = (
  isKubernetesOperatorControlled: boolean | null | undefined,
  configEntries?: RuntimeConfigEntry[] | null
): boolean => {
  const isBlockFlagEnabled =
    configEntries?.find((entry) => entry.key === RuntimeConfigKey.BLOCK_K8_OPERATOR)?.value ===
    'true';
  return isBlockFlagEnabled && !!isKubernetesOperatorControlled;
};

/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { Typography } from '@material-ui/core';

import { ProviderCode } from '../../constants';

import { YBProvider } from '../../types';

import styles from './K8sPropertiesOverview.module.scss';

interface K8sPropertiesOverviewProps {
  providerConfig: YBProvider;
  isProviderInUse: boolean;
}

export const K8sPropertiesOverview = ({
  providerConfig,
  isProviderInUse
}: K8sPropertiesOverviewProps) => {
  if (providerConfig.code !== ProviderCode.KUBERNETES) {
    return null;
  }

  return (
    <div className={styles.providerPropertiesContainer}>
      <div className={styles.propertiesRow}>
        <div>
          <Typography variant="body1">Usage</Typography>
          <Typography variant="body2">{isProviderInUse ? 'In Use' : 'Not In Use'}</Typography>
        </div>
      </div>
    </div>
  );
};

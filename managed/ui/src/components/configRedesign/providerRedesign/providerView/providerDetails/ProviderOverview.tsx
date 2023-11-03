/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { ProviderCode } from '../../constants';
import { RegionListOverview } from './RegionListOverview';
import { RegionMap } from '../../../../maps';
import { AWSPropertiesOverview } from '../aws/AWSPropertiesOverview';
import { AZUPropertiesOverview } from '../azu/AZUPropertiesOverview';
import { GCPPropertiesOverview } from '../gcp/GCPPropertiesOverview';
import { K8sPropertiesOverview } from '../k8s/K8sPropertiesOverview';
import { OnPremPropertiesOverview } from '../onPrem/OnPremPropertiesOverview';
import { assertUnreachableCase } from '../../../../../utils/errorHandlingUtils';

import { YBProvider } from '../../types';

import styles from './ProviderOverview.module.scss';

interface ProviderOverviewProps {
  providerConfig: YBProvider;
  isProviderInUse: boolean;
}

export const ProviderOverview = ({ providerConfig, isProviderInUse }: ProviderOverviewProps) => {
  return (
    <div className={styles.providerOverviewContainer}>
      {getProviderPropertiesOverview(providerConfig, isProviderInUse)}
      <div>
        <RegionMap
          title="All Supported Regions"
          regions={providerConfig.regions}
          type="Region"
          showLabels={false}
        />
      </div>
      <div className={styles.regionListContainer}>
        <RegionListOverview providerConfig={providerConfig} />
      </div>
    </div>
  );
};

const getProviderPropertiesOverview = (providerConfig: YBProvider, isProviderInUse: boolean) => {
  switch (providerConfig.code) {
    case ProviderCode.AWS:
      return (
        <AWSPropertiesOverview providerConfig={providerConfig} isProviderInUse={isProviderInUse} />
      );
    case ProviderCode.AZU:
      return (
        <AZUPropertiesOverview providerConfig={providerConfig} isProviderInUse={isProviderInUse} />
      );
    case ProviderCode.GCP:
      return (
        <GCPPropertiesOverview providerConfig={providerConfig} isProviderInUse={isProviderInUse} />
      );
    case ProviderCode.KUBERNETES:
      return (
        <K8sPropertiesOverview providerConfig={providerConfig} isProviderInUse={isProviderInUse} />
      );
    case ProviderCode.ON_PREM:
      return (
        <OnPremPropertiesOverview
          providerConfig={providerConfig}
          isProviderInUse={isProviderInUse}
        />
      );
    default:
      return assertUnreachableCase(providerConfig);
  }
};

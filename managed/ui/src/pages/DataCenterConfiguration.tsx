/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useQuery } from 'react-query';
import { Suspense, lazy } from 'react';
import DataCenterConfigurationContainer from '../components/config/ConfigProvider/DataCenterConfigurationContainer';
import { YBErrorIndicator, YBLoading, YBLoadingCircleIcon } from '../components/common/indicators';
import { api, runtimeConfigQueryKey } from '../redesign/helpers/api';
import { RuntimeConfigKey } from '../redesign/helpers/constants';
import { hasNecessaryPerm } from '../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../redesign/features/rbac/ApiAndUserPermMapping';
import { getWrappedChildren } from '../redesign/features/rbac/common/validator/ValidatorUtils';

const DataCenterConfigRedesignComponent = lazy(() =>
  import('../components/configRedesign/DataCenterConfigRedesign').then(
    ({ DataCenterConfigRedesign }) => ({
      default: DataCenterConfigRedesign
    })
  )
);

export const DataCenterConfiguration = (props: any) => {
  const customerUUID = localStorage.getItem('customerId') ?? '';
  const customerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.customerScope(customerUUID),
    () => api.fetchRuntimeConfigs(customerUUID, true)
  );

  const hasViewProviderPerm = hasNecessaryPerm(ApiPermissionMap.GET_PROVIDERS);

  if (!hasViewProviderPerm) {
    return getWrappedChildren({ overrideStyle: { marginTop: '150px' } });
  }

  if (customerRuntimeConfigQuery.isLoading || customerRuntimeConfigQuery.isIdle) {
    return <YBLoading />;
  }
  if (customerRuntimeConfigQuery.isError) {
    return (
      <YBErrorIndicator message="Error fetching runtime configurations for current customer." />
    );
  }
  const runtimeConfigEntries = customerRuntimeConfigQuery.data.configEntries ?? [];
  const shouldShowRedesignedUI = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.PROVIDER_REDESIGN_FEATURE_FLAG && config.value === 'true'
  );

  return (
    <>
      {shouldShowRedesignedUI ? (
        <>
          <Suspense fallback={YBLoadingCircleIcon}>
            <DataCenterConfigRedesignComponent {...props} />
          </Suspense>
        </>
      ) : (
        <DataCenterConfigurationContainer {...props} />
      )}
    </>
  );
};

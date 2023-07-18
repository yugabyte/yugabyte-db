/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { Box } from '@material-ui/core';

import { CloudVendorProviders, KubernetesProviderType, ProviderCode } from './constants';
import { ProviderDashboardView } from './InfraProvider';
import { ProviderList } from './ProviderList';

interface ProviderListViewCommonProps {
  setCurrentView: (newView: ProviderDashboardView) => void;
}
interface GenericProviderListViewProps extends ProviderListViewCommonProps {
  providerCode: typeof CloudVendorProviders[number] | typeof ProviderCode.ON_PREM;
}
interface K8sProviderListViewProps extends ProviderListViewCommonProps {
  providerCode: typeof ProviderCode.KUBERNETES;
  kubernetesProviderType: KubernetesProviderType;
}
type ProviderListViewProps = GenericProviderListViewProps | K8sProviderListViewProps;

export const ProviderListView = (props: ProviderListViewProps) => {
  const { providerCode, setCurrentView } = props;
  return (
    <Box padding="25px" minHeight="400px" bgcolor="white">
      {providerCode === ProviderCode.KUBERNETES ? (
        <ProviderList
          providerCode={providerCode}
          setCurrentView={setCurrentView}
          kubernetesProviderType={props.kubernetesProviderType}
        />
      ) : (
        <ProviderList providerCode={providerCode} setCurrentView={setCurrentView} />
      )}
    </Box>
  );
};

/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { Box, Typography } from '@material-ui/core';

import { YBButton } from '../../../redesign/components';
import {
  CloudVendorProviders,
  KubernetesProviderType,
  KubernetesProviderTypeLabel,
  ProviderCode,
  ProviderLabel
} from './constants';
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
      <Box display="flex" marginBottom="35px">
        <Typography variant="h4">{`${
          providerCode === ProviderCode.KUBERNETES && props.kubernetesProviderType
            ? KubernetesProviderTypeLabel[props.kubernetesProviderType]
            : ProviderLabel[providerCode]
        } Configs`}</Typography>
        <YBButton
          style={{ marginLeft: 'auto', width: '200px' }}
          variant="primary"
          onClick={() => setCurrentView(ProviderDashboardView.CREATE)}
          data-testid="ProviderListView-CreateConfigButton"
        >
          <i className="fa fa-plus" />
          Create Config
        </YBButton>
      </Box>
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

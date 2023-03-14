/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';

import { AWSProviderEditForm } from './aws/AWSProviderEditForm';
import { AZUProviderEditForm } from './azu/AZUProviderEditForm';
import { GCPProviderEditForm } from './gcp/GCPProviderEditForm';
import { K8sProviderEditForm } from './k8s/K8sProviderEditForm';
import { OnPremProviderEditForm } from './onPrem/OnPremProviderEditForm';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { YBProvider } from '../types';
import { ProviderCode } from '../constants';

interface ProviderEditViewProps {
  providerConfig: YBProvider;
}

export const ProviderEditView = ({ providerConfig }: ProviderEditViewProps) => {
  switch (providerConfig.code) {
    case ProviderCode.AWS:
      return <AWSProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.GCP:
      return <GCPProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.AZU:
      return <AZUProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.KUBERNETES:
      return <K8sProviderEditForm providerConfig={providerConfig} />;
    case ProviderCode.ON_PREM:
      return <OnPremProviderEditForm providerConfig={providerConfig} />;
    default: {
      return assertUnreachableCase(providerConfig);
    }
  }
};

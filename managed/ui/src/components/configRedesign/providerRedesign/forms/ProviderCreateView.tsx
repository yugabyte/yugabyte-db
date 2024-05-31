/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { AWSProviderCreateForm } from './aws/AWSProviderCreateForm';
import { AZUProviderCreateForm } from './azu/AZUProviderCreateForm';
import { CreateInfraProvider } from '../InfraProvider';
import { GCPProviderCreateForm } from './gcp/GCPProviderCreateForm';
import { K8sProviderCreateForm } from './k8s/K8sProviderCreateForm';
import { KubernetesProviderType, ProviderCode, CloudVendorProviders } from '../constants';
import { OnPremProviderCreateForm } from './onPrem/OnPremProviderCreateForm';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';

interface ProviderCreateViewCommonProps {
  handleOnBack: () => void;
  createInfraProvider: CreateInfraProvider;
}
interface GenericProviderCreateViewProps extends ProviderCreateViewCommonProps {
  providerCode: typeof CloudVendorProviders[number] | typeof ProviderCode.ON_PREM;
}
interface K8sProviderCreateViewProps extends ProviderCreateViewCommonProps {
  providerCode: typeof ProviderCode.KUBERNETES;
  kubernetesProviderType: KubernetesProviderType;
}

type ProviderCreateViewProps = GenericProviderCreateViewProps | K8sProviderCreateViewProps;

export const ProviderCreateView = (props: ProviderCreateViewProps) => {
  const { createInfraProvider, handleOnBack, providerCode } = props;
  switch (providerCode) {
    case ProviderCode.AWS:
      return (
        <AWSProviderCreateForm onBack={handleOnBack} createInfraProvider={createInfraProvider} />
      );
    case ProviderCode.GCP:
      return (
        <GCPProviderCreateForm onBack={handleOnBack} createInfraProvider={createInfraProvider} />
      );
    case ProviderCode.AZU:
      return (
        <AZUProviderCreateForm onBack={handleOnBack} createInfraProvider={createInfraProvider} />
      );
    case ProviderCode.KUBERNETES:
      return (
        <K8sProviderCreateForm
          onBack={handleOnBack}
          createInfraProvider={createInfraProvider}
          kubernetesProviderType={props.kubernetesProviderType}
        />
      );
    case ProviderCode.ON_PREM:
      return (
        <OnPremProviderCreateForm onBack={handleOnBack} createInfraProvider={createInfraProvider} />
      );
    default: {
      return assertUnreachableCase(providerCode);
    }
  }
};

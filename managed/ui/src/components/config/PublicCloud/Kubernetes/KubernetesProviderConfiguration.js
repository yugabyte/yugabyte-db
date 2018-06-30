// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoadingIcon } from '../../../common/indicators';
import { withRouter } from 'react-router';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import ListKubernetesConfigurations from './ListKubernetesConfigurations';
import CreateKubernetesConfigurationContainer from './CreateKubernetesConfigurationContainer';
import { KUBERNETES_PROVIDERS } from 'config';

const PROVIDER_TYPE = "kubernetes";

class KubernetesProviderConfiguration extends Component {
  constructor(props) {
    super(props);
    this.state = {
      listView: true
    };
  }

  toggleListView = () => {
    this.setState({listView: !this.state.listView});
  }

  render() {
    const { providers, regions, type } = this.props;

    if (getPromiseState(providers).isLoading() ||
        getPromiseState(providers).isInit()) {
      return <YBLoadingIcon size="medium" />;
    }

    const kubernetesRegions = regions.data.filter((region) => region.provider.code === PROVIDER_TYPE);
    const configuredProviderData = kubernetesRegions.map((region) => {
      const providerData = providers.data.find((p) => p.uuid === region.provider.uuid);
      const providerConfig = providerData.config;
      if (providerConfig['KUBECONFIG_PROVIDER'] !== type) {
        return null;
      }
      const providerTypeMetadata = KUBERNETES_PROVIDERS.find((providerType) => providerType.code === providerConfig['KUBECONFIG_PROVIDER']);
      return {
        "uuid": region.provider.uuid,
        "name": region.provider.name,
        "region": region.name,
        "zones": region.zones.map((zone) => zone.name).join(", "),
        "configPath": providerConfig['KUBECONFIG'],
        "type": providerTypeMetadata && providerTypeMetadata.name
      };
    }).filter(Boolean);

    if (this.state.listView && isNonEmptyArray(configuredProviderData)) {
      return (
        <ListKubernetesConfigurations
          providers={configuredProviderData}
          onCreate={this.toggleListView}
          type={type} />
      );
    } else {
      return (
        <CreateKubernetesConfigurationContainer
          onCancel={this.toggleListView}
          onSubmit={this.toggleListView}
          type={type} />
      );
    }
  }
}

export default withRouter(KubernetesProviderConfiguration);

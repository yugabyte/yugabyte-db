// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoadingIcon } from '../../../common/indicators';
import { withRouter } from 'react-router';
import { isNonEmptyArray, isDefinedNotNull } from 'utils/ObjectUtils';
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

  toggleListView = (value) => {
    if (typeof(value) === typeof(true)) {
      this.setState({listView: value});
    } else {
      this.setState({listView: !this.state.listView});
    }
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
      if (!isDefinedNotNull(providerData) || providerData.config['KUBECONFIG_PROVIDER'] !== type) {
        return null;
      }
      const providerTypeMetadata = KUBERNETES_PROVIDERS.find((providerType) => providerType.code === providerData.config['KUBECONFIG_PROVIDER']);
      return {
        "uuid": region.provider.uuid,
        "name": region.provider.name,
        "region": region.name,
        "zones": region.zones.map((zone) => zone.name).join(", "),
        "configPath": providerData.config['KUBECONFIG'],
        "type": providerTypeMetadata && providerTypeMetadata.name
      };
    }).filter(Boolean);

    if (this.state.listView && isNonEmptyArray(configuredProviderData)) {
      return (
        <ListKubernetesConfigurations
          providers={configuredProviderData}
          onCreate={this.toggleListView}
          deleteProviderConfig={this.props.deleteProviderConfig}
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

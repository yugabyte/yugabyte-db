// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { KubernetesProviderConfiguration } from '../../../config';

const mapStateToProps = (state) => {
  return {
    providers: state.cloud.providers,
    regions: state.cloud.supportedRegionList
  };
};

export default connect(mapStateToProps)(KubernetesProviderConfiguration);

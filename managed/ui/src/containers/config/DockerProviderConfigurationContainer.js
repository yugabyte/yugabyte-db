// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import DockerProviderConfiguration from '../../components/config/DockerProviderConfiguration';

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

export default connect(mapStateToProps)(DockerProviderConfiguration);

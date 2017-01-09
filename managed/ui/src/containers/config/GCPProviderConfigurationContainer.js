// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import GCPProviderConfiguration from '../../components/config/GCPProviderConfiguration';


const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

export default connect(mapStateToProps)(GCPProviderConfiguration);

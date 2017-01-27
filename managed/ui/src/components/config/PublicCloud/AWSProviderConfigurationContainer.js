// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { AWSProviderConfiguration } from '../../config';


const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

export default connect(mapStateToProps)(AWSProviderConfiguration);

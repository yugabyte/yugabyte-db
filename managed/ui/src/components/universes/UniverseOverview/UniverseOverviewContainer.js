// Copyright YugaByte Inc.

import { UniverseOverview } from './UniverseOverview';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer,
    layout: state.customer.layout
  };
}

export default connect(mapStateToProps)(UniverseOverview);

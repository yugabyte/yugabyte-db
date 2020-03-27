// Copyright YugaByte Inc.

import { UniverseInfoPanel } from '../../panels';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer
  };
}

export default connect(mapStateToProps)(UniverseInfoPanel);

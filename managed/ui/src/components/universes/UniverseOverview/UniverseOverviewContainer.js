// Copyright YugaByte Inc.
// eslint-disable-next-line import/named
import { UniverseOverview } from '../../universes';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer,
    layout: state.customer.layout
  };
}

export default connect(mapStateToProps)(UniverseOverview);

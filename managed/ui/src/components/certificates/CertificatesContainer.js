// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import Certificates from './Certificates';

function mapStateToProps(state) {
  return {
    refreshReleases: state.customer.refreshReleases,
    customer: state.customer
  };
}

export default connect(mapStateToProps, null)(Certificates);

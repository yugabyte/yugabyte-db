// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import DashboardRightPane from '../components/DashboardRightPane.js';

const mapStateToProps = (state) => {
  return {
    regions: state.regions
  };
}

export default connect(mapStateToProps)(DashboardRightPane);

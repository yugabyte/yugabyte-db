// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { SupportItems } from '../../support';

const mapDispatchToProps = (dispatch) => {
  return {

  }
}

function mapStateToProps(state, ownProps) {
  return {
    customer: state.customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(SupportItems);

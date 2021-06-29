// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nissharma@deloitte.com)
//
// TODO: Redux still needs to be configured once the API contract
// will be finalized and be available.

import { connect } from 'react-redux';
import { alertConfigs, alertDestionations } from '../../../actions/customers';
import { AlertConfiguration } from './AlertConfiguration';

const mapStateToProps = (state) => {
  // console.log(state' ******** state');
  return {};
};

const mapDispatchToProps = (dispatch) => {
  return {
    alertConfigs: () => {
      return dispatch(alertConfigs());
    },

    alertDestionations: () => {
      return dispatch(alertDestionations());
    }
  };
};

export default connect(mapStateToProps, mapDispatchToProps)(AlertConfiguration);

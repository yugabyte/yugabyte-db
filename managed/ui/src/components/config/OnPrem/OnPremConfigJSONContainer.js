// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import {OnPremConfigJSON} from '../../config';
import {setOnPremConfigData} from '../../../actions/config';

const mapStateToProps = (state) => {
  return {
    cloud: state.cloud,
    config: state.config
  };
};

const mapDispatchToProps = (dispatch) => {
  return {
    setOnPremJsonData:(formData) => {
      dispatch(setOnPremConfigData(formData))
    }
  }
};

export default connect(mapStateToProps, mapDispatchToProps)(OnPremConfigJSON);

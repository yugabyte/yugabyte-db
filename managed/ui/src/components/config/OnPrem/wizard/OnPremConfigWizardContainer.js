// Copyright (c) YugabyteDB, Inc.

import { connect } from 'react-redux';
import OnPremConfigWizard from './OnPremConfigWizard';

const mapStateToProps = (state) => {
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData
  };
};

export default connect(mapStateToProps)(OnPremConfigWizard);

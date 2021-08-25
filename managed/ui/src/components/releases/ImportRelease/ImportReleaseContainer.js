// Copyright (c) Yugabyte, Inc.

import { connect } from 'react-redux';
import { ImportRelease } from '../../../components/releases';
import { importYugabyteRelease, importYugabyteReleaseResponse } from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    importYugabyteRelease: (payload) => {
      dispatch(importYugabyteRelease(payload)).then((response) => {
        dispatch(importYugabyteReleaseResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    initialValues: { version: '' },
    importRelease: state.customer.importRelease
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ImportRelease);

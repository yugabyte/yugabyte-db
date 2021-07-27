// Copyright (c) Yugabyte, Inc.

import { connect } from 'react-redux';
import { UpdateRelease } from '../../../components/releases';
import { updateYugabyteRelease, updateYugabyteReleaseResponse } from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    updateYugabyteRelease: (version, payload) => {
      dispatch(updateYugabyteRelease(version, payload)).then((response) => {
        dispatch(updateYugabyteReleaseResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    updateRelease: state.customer.updateRelease
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UpdateRelease);

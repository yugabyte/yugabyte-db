// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { toast } from 'react-toastify';
import { UpdateRelease } from '../../../components/releases';
import {
  deleteYugaByteRelease,
  fetchSoftwareVersionsFailure,
  fetchSoftwareVersionsSuccess,
  getYugaByteReleases,
  getYugaByteReleasesResponse,
  updateYugaByteRelease,
  updateYugaByteReleaseResponse
} from '../../../actions/customers';
import { createErrorMessage } from '../../../utils/ObjectUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    updateYugaByteRelease: (version, payload) => {
      dispatch(updateYugaByteRelease(version, payload)).then((response) => {
        dispatch(updateYugaByteReleaseResponse(response.payload));
      });
    },
    deleteYugaByteRelease: (version) => {
      dispatch(deleteYugaByteRelease(version)).then((response) => {
        if (response.error) toast.error(createErrorMessage(response.payload));
        else
          dispatch(getYugaByteReleases()).then((response) => {
            dispatch(getYugaByteReleasesResponse(response.payload));
            if (response.payload.status !== 200) {
              dispatch(fetchSoftwareVersionsFailure(response.payload));
            } else {
              dispatch(fetchSoftwareVersionsSuccess(response.payload));
            }
          });
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    updateRelease: state.customer.updateRelease
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UpdateRelease);

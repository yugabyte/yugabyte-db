// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { ReleaseList } from '../../../components/releases';
import {
  refreshYugaByteReleases,
  refreshYugaByteReleasesResponse,
  getYugaByteReleases,
  getYugaByteReleasesResponse,
  fetchSoftwareVersionsFailure,
  fetchSoftwareVersionsSuccess
} from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    refreshYugaByteReleases: () => {
      dispatch(refreshYugaByteReleases()).then((response) => {
        dispatch(refreshYugaByteReleasesResponse(response.payload));
      });
    },
    getYugaByteReleases: () => {
      dispatch(getYugaByteReleases()).then((response) => {
        dispatch(getYugaByteReleasesResponse(response.payload));
        if (response.payload.status !== 200) {
          dispatch(fetchSoftwareVersionsFailure(response.payload));
        } else {
          dispatch(fetchSoftwareVersionsSuccess(response.payload));
        }
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    refreshReleases: state.customer.refreshReleases,
    releases: state.customer.releases,
    customer: state.customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ReleaseList);

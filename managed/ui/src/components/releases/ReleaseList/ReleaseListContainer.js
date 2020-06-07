// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { ReleaseList } from '../../../components/releases';
import {
  refreshYugaByteReleases,
  refreshYugaByteReleasesResponse,
  getYugaByteReleases,
  getYugaByteReleasesResponse
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

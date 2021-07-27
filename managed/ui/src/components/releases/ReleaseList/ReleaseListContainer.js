// Copyright Yugabyte Inc.

import { connect } from 'react-redux';
import { ReleaseList } from '../../../components/releases';
import {
  refreshYugabyteReleases,
  refreshYugabyteReleasesResponse,
  getYugabyteReleases,
  getYugabyteReleasesResponse
} from '../../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    refreshYugabyteReleases: () => {
      dispatch(refreshYugabyteReleases()).then((response) => {
        dispatch(refreshYugabyteReleasesResponse(response.payload));
      });
    },
    getYugabyteReleases: () => {
      dispatch(getYugabyteReleases()).then((response) => {
        dispatch(getYugabyteReleasesResponse(response.payload));
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

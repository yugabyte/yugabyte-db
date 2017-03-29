// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import AuthenticatedComponent from './AuthenticatedComponent';
import { fetchHostInfo, fetchHostInfoSuccess,
  fetchHostInfoFailure } from '../../actions/customers';
import { fetchUniverseList, fetchUniverseListSuccess,
  fetchUniverseListFailure, resetUniverseList
} from '../../actions/universe';
import { getRegionList, getRegionListSuccess,
  getRegionListFailure, getProviderList,
  getProviderListSuccess, getProviderListFailure, getSupportedRegionData,
  getSupportedRegionDataFailure, getSupportedRegionDataSuccess, listAccessKeys,
  listAccessKeysSuccess, listAccessKeysFailure}
  from '../../actions/cloud';
import {fetchSoftwareVersions, fetchSoftwareVersionsSuccess, fetchSoftwareVersionsFailure} from 'actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList())
        .then((response) => {
          if (response.payload.status !== 200) {
            dispatch(fetchUniverseListFailure(response.payload));
          } else {
            dispatch(fetchUniverseListSuccess(response.payload));
          }
        });
    },

    getProviderListItems: () => {
      dispatch(getProviderList()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getProviderListFailure(response.payload));
        } else {
          dispatch(getProviderListSuccess(response.payload));
          response.payload.data.forEach(function (item, idx) {
            dispatch(listAccessKeys(item.uuid))
              .then((response) => {
                if (response.payload.status !== 200) {
                  dispatch(listAccessKeysFailure(response.payload));
                } else {
                  dispatch(listAccessKeysSuccess(response.payload));
                }
              });
            dispatch(getRegionList(item.uuid, true))
              .then((response) => {
                if (response.payload.status !== 200) {
                  dispatch(getRegionListFailure(response.payload));
                } else {
                  dispatch(getRegionListSuccess(response.payload));
                }
              });
          })}
      });
    },

    getSupportedRegionList: () => {
      dispatch(getSupportedRegionData()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getSupportedRegionDataFailure(response.payload));
        } else {
          dispatch(getSupportedRegionDataSuccess(response.payload));
        }
      })
    },
    resetUniverseList: () => {
      dispatch(resetUniverseList());
    },
    fetchSoftwareVersions: () => {
      dispatch(fetchSoftwareVersions()).then((response)=>{
        if (response.payload.status !== 200) {
          dispatch(fetchSoftwareVersionsFailure(response.payload));
        } else {
          dispatch(fetchSoftwareVersionsSuccess(response.payload));
        }
      })
    },
    fetchHostInfo: () => {
      dispatch(fetchHostInfo()).then((response)=>{
        if (response.payload.status !== 200) {
          dispatch(fetchHostInfoFailure(response.payload));
        } else {
          dispatch(fetchHostInfoSuccess(response.payload));
        }
      })
    }
  }
}

const mapStateToProps = (state) => {
  return {
    customer: state.customer,
    universe: state.universe,
    cloud: state.cloud
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(AuthenticatedComponent);

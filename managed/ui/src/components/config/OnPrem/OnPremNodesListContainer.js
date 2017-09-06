// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import { reset } from 'redux-form';
import  OnPremNodesList from './OnPremNodesList';
import { getInstanceTypeList, getInstanceTypeListResponse, getRegionList, getRegionListResponse,
  createNodeInstances, createNodeInstancesResponse, getNodeInstancesForProvider,
  getNodesInstancesForProviderResponse } from '../../../actions/cloud';
import { reduxForm } from 'redux-form';
import {openDialog, closeDialog} from '../../../actions/universe';

const mapStateToProps = (state) => {
  return {
    cloud: state.cloud,
    visibleModal: state.universe.visibleModal
  };
};

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    getInstanceTypeListItems: (provider) => {
      dispatch(getInstanceTypeList(provider)).then((response) => {
        dispatch(getInstanceTypeListResponse(response.payload));
      });
    },

    getRegionListItems: (provider, isMultiAZ) => {
      dispatch(getRegionList(provider, isMultiAZ)).then((response) => {
        dispatch(getRegionListResponse(response.payload));
      });
    },

    createOnPremNodes: (nodePayloadData, pUUID) => {
      nodePayloadData.forEach(function(nodePayload){
        Object.keys(nodePayload).forEach((zoneUUID, zoneIdx) => {
          const nodesForZone = nodePayload[zoneUUID];
          dispatch(createNodeInstances(zoneUUID, nodesForZone)).then((response) => {
            dispatch(createNodeInstancesResponse(response.payload));
            if (zoneIdx === Object.keys(nodePayload).length -1) {
              dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
                dispatch(getNodesInstancesForProviderResponse(response.payload));
              });
              dispatch(closeDialog());
            }
          });
        });
      });
    },

    showAddNodesDialog() {
      dispatch(openDialog("AddNodesForm"));
    },

    hideAddNodesDialog() {
      dispatch(closeDialog());
      dispatch(reset("AddNodeForm"));
    },

    fetchConfiguredNodeList: (pUUID) => {
      dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    }
  };
};

const addNodeForm = reduxForm({
  form: 'AddNodeForm',

});

export default connect(mapStateToProps, mapDispatchToProps)(addNodeForm(OnPremNodesList));

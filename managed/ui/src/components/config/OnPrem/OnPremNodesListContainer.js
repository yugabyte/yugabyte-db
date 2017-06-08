// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';

import {reset} from 'redux-form';
import  OnPremNodesList from './OnPremNodesList';
import { getInstanceTypeList, getInstanceTypeListResponse, getRegionList, getRegionListResponse, createNodeInstance,
         createNodeInstanceResponse, getNodeInstancesForProvider, getNodesInstancesForProviderResponse } from '../../../actions/cloud';
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
    createOnPremNodes: (nodePayload, pUUID) => {
      nodePayload.forEach(function(payloadItem, payloadIdx){
        Object.keys(payloadItem).forEach(function(instanceTypeKey, keyIdx){
          var instanceTypeArray = payloadItem[instanceTypeKey];
          instanceTypeArray.forEach(function(instanceTypeItem, instanceTypeIdx){
            dispatch(createNodeInstance(instanceTypeKey, instanceTypeItem)).then((response) => {
              dispatch(createNodeInstanceResponse(response.payload));
              if (payloadIdx === (nodePayload.length -1) && keyIdx === (Object.keys(payloadItem).length - 1) && instanceTypeIdx === (instanceTypeArray.length -1)) {
                dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
                  dispatch(getNodesInstancesForProviderResponse(response.payload));
                });
                dispatch(closeDialog());
              }
            });
          })
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
  }
};

var addNodeForm = reduxForm({
  form: 'AddNodeForm',

});

export default connect(mapStateToProps, mapDispatchToProps)(addNodeForm(OnPremNodesList));

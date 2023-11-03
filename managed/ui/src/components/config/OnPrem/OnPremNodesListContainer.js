// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { isNonEmptyObject, isNonEmptyArray, isNonEmptyString } from '../../../utils/ObjectUtils';
import { reset , reduxForm } from 'redux-form';
import { toast } from 'react-toastify';
import OnPremNodesList from './OnPremNodesList';
import {
  getInstanceTypeList,
  getInstanceTypeListResponse,
  getRegionList,
  getRegionListResponse,
  createNodeInstances,
  createNodeInstancesResponse,
  getNodeInstancesForProvider,
  getNodesInstancesForProviderResponse,
  precheckInstance,
  precheckInstanceResponse,
  deleteInstance,
  deleteInstanceResponse
} from '../../../actions/cloud';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../actions/tasks';
import { fetchUniverseList, fetchUniverseListResponse , closeUniverseDialog } from '../../../actions/universe';


import { openDialog, closeDialog } from '../../../actions/modal';

const mapStateToProps = (state) => {
  return {
    cloud: state.cloud,
    tasks: state.tasks,
    universeList: state.universe.universeList,
    visibleModal: state.modal.visibleModal
  };
};

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
      });
    },

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
      nodePayloadData.forEach(function (nodePayload) {
        Object.keys(nodePayload).forEach((zoneUUID, zoneIdx) => {
          const nodesForZone = nodePayload[zoneUUID];          
          dispatch(createNodeInstances(zoneUUID, nodesForZone)).then((response) => {       
            if (!response.error) {
              dispatch(createNodeInstancesResponse(response.payload));
              if (zoneIdx === Object.keys(nodePayload).length - 1) {
                dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
                  dispatch(getNodesInstancesForProviderResponse(response.payload));
                });
                dispatch(closeDialog());
                dispatch(closeUniverseDialog());
              }
            } else {
              const errorMessage = response.payload?.response?.data?.error ?? 'Something went wrong creating node instances!';
              toast.error(errorMessage);
              dispatch(closeDialog());
            }           
          });
        });
      });
    },

    showAddNodesDialog() {
      dispatch(openDialog('AddNodesForm'));
    },

    hideAddNodesDialog() {
      dispatch(closeDialog());
      dispatch(closeUniverseDialog());
      dispatch(reset('AddNodeForm'));
    },

    fetchConfiguredNodeList: (pUUID) => {
      dispatch(getNodeInstancesForProvider(pUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    },

    fetchCustomerTasks: () => {
      dispatch(fetchCustomerTasks()).then((response) => {
        if (!response.error) {
          dispatch(fetchCustomerTasksSuccess(response.payload));
        } else {
          dispatch(fetchCustomerTasksFailure(response.payload));
        }
      });
    },

    precheckInstance: (providerUUID, instanceIP) => {
      dispatch(precheckInstance(providerUUID, instanceIP)).then((response) => {
        dispatch(precheckInstanceResponse(response.payload));
      });
    },

    deleteInstance: (providerUUID, instanceIP) => {
      dispatch(deleteInstance(providerUUID, instanceIP)).then((response) => {
        dispatch(deleteInstanceResponse(response.payload));
        if (response.payload.status === 200) {
          dispatch(getNodeInstancesForProvider(providerUUID)).then((response) => {
            dispatch(getNodesInstancesForProviderResponse(response.payload));
          });
        }
      });
    },

    showConfirmDeleteModal: () => {
      dispatch(openDialog('confirmDeleteNodeInstance'));
    },

    showConfirmPrecheckModal: () => {
      dispatch(openDialog('confirmPrecheckNodeInstance'));
    },

    hideDialog: () => {
      dispatch(closeDialog());
      dispatch(closeUniverseDialog());
    }
  };
};

const validate = (values) => {
  const errors = { instances: {} };
  if (isNonEmptyObject(values.instances)) {
    Object.keys(values.instances).forEach((instanceRowKey) => {
      const instanceRowArray = values.instances[instanceRowKey];
      errors.instances[instanceRowKey] = [];
      if (isNonEmptyArray(instanceRowArray)) {
        instanceRowArray.forEach((instanceRowItem, instanceRowIdx) => {
          const instanceErrors = {};
          if (Object.keys(instanceRowItem).length) {
            if (!instanceRowItem.zone) {
              instanceErrors.zone = 'Zone is required';
            }
            if (!instanceRowItem.instanceTypeIP) {
              instanceErrors.instanceTypeIP = 'IP address or DNS is required';
            }
            if (!instanceRowItem.machineType) {
              instanceErrors.machineType = 'Type is required';
            }
            if (
              isNonEmptyString(instanceRowItem.instanceTypeIP) &&
              instanceRowItem.instanceTypeIP.length > 75
            ) {
              instanceErrors.instanceTypeIP = 'Address Too Long';
            }
          }          
          errors.instances[instanceRowKey][instanceRowIdx] = instanceErrors;
        });
      }
    });
  }
  return errors;
};

const addNodeForm = reduxForm({
  form: 'AddNodeForm',
  validate,
  asyncBlurFields: []
});

export default connect(mapStateToProps, mapDispatchToProps)(addNodeForm(OnPremNodesList));

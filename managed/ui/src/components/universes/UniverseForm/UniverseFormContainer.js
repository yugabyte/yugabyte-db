// Copyright (c) YugaByte, Inc.

import { reduxForm, formValueSelector } from 'redux-form';
import { connect } from 'react-redux';
import { fetchCustomerTasks, fetchCustomerTasksSuccess, fetchCustomerTasksFailure } from '../../../actions/tasks';
import UniverseForm from './UniverseForm';
import { getInstanceTypeList, getRegionList, getRegionListResponse, getInstanceTypeListResponse,
         listAccessKeys, listAccessKeysResponse, getNodeInstancesForProvider, getNodesInstancesForProviderResponse } from 'actions/cloud';
import { createUniverse, createUniverseResponse, editUniverse, editUniverseResponse, closeDialog,
         configureUniverseTemplate, configureUniverseTemplateResponse, configureUniverseTemplateSuccess,
         configureUniverseResources, configureUniverseResourcesResponse,
         checkIfUniverseExists, setPlacementStatus, resetUniverseConfiguration,
         fetchUniverseInfo, fetchUniverseInfoResponse, fetchUniverseMetadata, fetchUniverseTasks, fetchUniverseTasksResponse } from 'actions/universe';
import { isDefinedNotNull, isNonEmptyObject } from 'utils/ObjectUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    submitConfigureUniverse: (values) => {
      dispatch(configureUniverseTemplate(values)).then((response) => {
        dispatch(configureUniverseTemplateResponse(response.payload));
      });
    },

    fetchUniverseResources: (payload) => {
      dispatch(configureUniverseResources(payload)).then((resourceData) => {
        dispatch(configureUniverseResourcesResponse(resourceData.payload));
      });
    },


    submitCreateUniverse: (values) => {
      dispatch(createUniverse(values)).then((response) => {
        dispatch(createUniverseResponse(response.payload));
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

    fetchUniverseMetadata: () => {
      dispatch(fetchUniverseMetadata());
    },

    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    },

    closeUniverseDialog: () => {
      dispatch(closeDialog());
    },

    submitEditUniverse: (values, universeUUID) => {
      dispatch(editUniverse(values, universeUUID)).then((response) => {
        dispatch(editUniverseResponse(response.payload));
      });
    },

    getInstanceTypeListItems: (provider) => {
      dispatch(getInstanceTypeList(provider)).then((response) => {
        dispatch(getInstanceTypeListResponse(response.payload));
      });
    },

    getAccessKeys: (provider) => {
      dispatch(listAccessKeys(provider)).then((response) => {
        dispatch(listAccessKeysResponse(response.payload));
      })
    },

    getRegionListItems: (provider, isMultiAZ) => {
      dispatch(getRegionList(provider, isMultiAZ)).then((response) => {
        dispatch(getRegionListResponse(response.payload));
      });
    },

    setPlacementStatus: (currentStatus) => {
      dispatch(setPlacementStatus(currentStatus));
    },

    resetConfig: () => {
      dispatch(resetUniverseConfiguration());
    },

    fetchUniverseTasks: (universeUUID) => {
      dispatch(fetchUniverseTasks(universeUUID)).then((response) => {
        dispatch(fetchUniverseTasksResponse(response.payload));
      })
    },
    getExistingUniverseConfiguration: (universeDetail) => {
      dispatch(configureUniverseTemplateSuccess({data: universeDetail}));
      dispatch(configureUniverseResources(universeDetail)).then((resourceData) => {
        dispatch(configureUniverseResourcesResponse(resourceData.payload));
      });
    },

    fetchNodeInstanceList: (providerUUID) => {
      dispatch(getNodeInstancesForProvider(providerUUID)).then((response) => {
        dispatch(getNodesInstancesForProviderResponse(response.payload));
      });
    }
  }
};

const formFieldNames = ['formType', 'universeName', 'provider',  'providerType', 'regionList',
  'numNodes', 'isMultiAZ', 'instanceType', 'ybSoftwareVersion', 'azSelectorFields', 'accessKeyCode'];

function mapStateToProps(state, ownProps) {
  const {universe: { currentUniverse }} = state;
  let data = {
    "universeName": "",
    "ybSoftwareVersion": "",
    "numNodes": 3,
    "isMultiAZ": true,
    "instanceType": "m3.medium",
    "formType": "create",
    "accessKeyCode": "yugabyte-default"
  };
  if (isNonEmptyObject(currentUniverse.data) && ownProps.type === "Edit") {
    let userIntent = currentUniverse.data.universeDetails && currentUniverse.data.universeDetails.userIntent;
    data.universeName = currentUniverse.data.name;
    data.formType = "edit";
    data.provider = currentUniverse.data.provider && currentUniverse.data.provider.uuid;
    data.numNodes = userIntent && userIntent.numNodes;
    data.isMultiAZ = userIntent && userIntent.isMultiAZ;
    data.instanceType = userIntent && userIntent.instanceType;
    data.ybSoftwareVersion = userIntent && userIntent.ybSoftwareVersion;
    data.accessKeyCode = userIntent && userIntent.accessKeyCode;
    if (isDefinedNotNull(currentUniverse.data.universeDetails)  && userIntent.isMultiAZ) {
      data.regionList = currentUniverse.data.regions && currentUniverse.data.regions.map(function (item, idx) {
        return {value: item.uuid, name: item.name, label: item.name};
      })
    } else {
      data.regionList = [{
        value: currentUniverse.data.regions[0].uuid,
        name: currentUniverse.data.regions[0].name,
        label: currentUniverse.data.regions[0].name
      }];
    }
  }

  const selector = formValueSelector('UniverseForm');

  return {
    universe: state.universe,
    tasks: state.tasks,
    cloud: state.cloud,
    softwareVersions: state.customer.softwareVersions,
    accessKeys: state.cloud.accessKeys,
    initialValues: data,
    formValues: selector(state,
      'formType', 'universeName', 'provider', 'providerType', 'regionList',
      'numNodes', 'isMultiAZ', 'instanceType', 'ybSoftwareVersion', 'accessKeyCode')
  };
}

const asyncValidate = (values, dispatch ) => {
  return new Promise((resolve, reject) => {
    dispatch(checkIfUniverseExists(values.universeName)).then((response) => {
      if (response.payload.status !== 200 && values.formType !== "edit") {
        reject({universeName: 'Universe name already exists'});
      } else {
        resolve();
      }
    })
  })
};

const validate = values => {
  const errors = {};
  if (!isDefinedNotNull(values.universeName)) {
    errors.universeName = 'Universe Name is Required'
  }
  if (!isDefinedNotNull(values.provider)) {
    errors.provider = 'Provider Value is Required'
  }
  if (!isDefinedNotNull(values.regionList)) {
    errors.regionList = 'Region Value is Required'
  }
  if (!isDefinedNotNull(values.instanceType)) {
    errors.instanceType = 'Instance Type is Required'
  }
  return errors;
};

var universeForm = reduxForm({
  form: 'UniverseForm',
  validate,
  asyncValidate,
  asyncBlurFields: ['universeName'],
  fields: formFieldNames
});

export default connect(mapStateToProps, mapDispatchToProps)(universeForm(UniverseForm));

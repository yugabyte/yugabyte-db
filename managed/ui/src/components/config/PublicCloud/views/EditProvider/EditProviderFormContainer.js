// Copyright (c) YugaByte, Inc.

import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import { isNonEmptyString } from '../../../../../utils/ObjectUtils';
import {
  editProvider,
  editProviderResponse,
  fetchCloudMetadata
} from '../../../../../actions/cloud';
import EditProviderForm from './EditProviderForm';
import { fetchUniverseList, fetchUniverseListResponse } from '../../../../../actions/universe';

const mapDispatchToProps = (dispatch) => {
  return {
    submitEditProvider: (payload) => {
      dispatch(editProvider(payload)).then((response) => {
        dispatch(editProviderResponse(response.payload));
      });
    },

    reloadCloudMetadata: () => {
      dispatch(fetchCloudMetadata());
    },
    fetchUniverseList: () => {
      dispatch(fetchUniverseList()).then((response) => {
        dispatch(fetchUniverseListResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    initialValues: {
      accountName: ownProps.accountName,
      accountUUID: ownProps.uuid,
      secretKey: ownProps.sshKey,
      hostedZoneId: ownProps.hostedZoneId
    },
    editProvider: state.cloud.editProvider,
    universeList: state.universe.universeList
  };
}

export const validateEditProvider = (values, props) => {
  const errors = {};
  if (!isNonEmptyString(values.hostedZoneId)) {
    errors.hostedZoneId = 'Cannot be empty';
  }
  return errors;
};

const editProviderForm = reduxForm({
  form: 'EditProviderForm',
  validate: validateEditProvider,
  fields: ['hostedZoneId']
});

export default connect(mapStateToProps, mapDispatchToProps)(editProviderForm(EditProviderForm));

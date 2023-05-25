// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { ImportRelease } from '../../../components/releases';
import { importYugaByteRelease, importYugaByteReleaseResponse } from '../../../actions/customers';
import { toast } from 'react-toastify';

const createErrorMessage = (payload) => {
  const structuredError = payload?.response?.data?.error;
  if (structuredError) {
    if (typeof structuredError === 'string') {
      return structuredError;
    }
    const message = Object.keys(structuredError)
      .map((fieldName) => {
        const messages = structuredError[fieldName];
        return fieldName + ': ' + messages.join(', ');
      })
      .join('\n');
    return message;
  }
  return payload.message;
};

const mapDispatchToProps = (dispatch, props) => {
  return {
    importYugaByteRelease: (payload) => {
      dispatch(importYugaByteRelease(payload)).then((response) => {
        if (response.error) {
          toast.error(createErrorMessage(response.payload));
        } else {
          toast.success('Release imported successfully!.');
          props.onHide();
          props.onModalSubmit();
          dispatch(importYugaByteReleaseResponse(response.payload));
        }
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  const import_types = ['s3', 'gcs', 'http'].map((type) => {
    return { value: type, label: type };
  });
  return {
    onModalSubmit: ownProps.onModalSubmit,
    import_types,
    initialValues: { version: '', import_type: import_types[0] },
    importRelease: state.customer.importRelease
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(ImportRelease);

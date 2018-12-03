// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { Importer } from '../importer';
import { reduxForm } from 'redux-form';
import {importUniverse, importUniverseResponse} from '../../actions/universe';

//Client side validation
function validate(values) {
  const errors = {};
  let hasErrors = false;

  if (!values.universeName || values.universeName.trim() === '') {
    errors.name = 'Enter a universe name';
    hasErrors = true;
  }

  if (!values.masterAddresses || values.masterAddresses.trim() === '') {
    errors.masterAddresses = 'Enter the master addresses of the universe';
    hasErrors = true;
  }

  return hasErrors && errors;
}

const mapDispatchToProps = (dispatch) => {
  return {
    importUniverse: (values) => {
      return dispatch(importUniverse(values)).then((response) => {
        dispatch(importUniverseResponse(response.payload));
      });
    }
  };
};

const importUniverseForm = reduxForm({
  form: 'ImportUniverse',
  fields: ['universeName', 'cloudProviderType', 'masterAddresses'],
  validate
});


function mapStateToProps(state) {
  return {
    universeImport: state.universe.universeImport,
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(importUniverseForm(Importer));

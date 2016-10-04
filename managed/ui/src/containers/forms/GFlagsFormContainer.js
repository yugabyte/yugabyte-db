// Copyright (c) YugaByte, Inc.

import GFlagsForm from '../../components/forms/GFlagsForm';
import { reduxForm } from 'redux-form';
import { connect } from 'react-redux';
import {submitGFlags} from '../../actions/universe';
const mapDispatchToProps = (dispatch) => {
  return {
    submitGFlagsForm: (values) => {
      var payload = values.gFlags.reduce(function(map, item) {
        map[item.name] = item.value;
        return map;
      }, {});
      dispatch(submitGFlags(payload)).then((response) => {
        // Add API and Reducer Logic
      })
    }
  }
}

var gFlagsForm = reduxForm({
  form: 'GFlagsForm'
})

module.exports = connect(mapDispatchToProps)(gFlagsForm(GFlagsForm));

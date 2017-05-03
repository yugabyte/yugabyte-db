// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import {CustomerProfile} from '../profile';
import { reduxForm } from 'redux-form';
import {updateProfile, updateProfileSuccess, updateProfileFailure} from '../../actions/customers';
import {isValidObject} from '../../utils/ObjectUtils';
import { SubmissionError } from 'redux-form'

//Client side validation
function validate(values) {
  var errors = {};
  var hasErrors = false;

  if (!values.name || values.name.trim() === '') {
    errors.name = 'Enter a name';
    hasErrors = true;
  }

  if(!values.password || values.password.trim() === '') {
    errors.password = 'Enter password';
    hasErrors = true;
  }
  if(!values.confirmPassword || values.confirmPassword.trim() === '') {
    errors.confirmPassword = 'Enter Confirm Password';
    hasErrors = true;
  }

  if(values.confirmPassword  && values.confirmPassword.trim() !== '' &&
    values.password  && values.password.trim() !== '' &&
    values.password !== values.confirmPassword) {
    errors.password = 'Password And Confirm Password don\'t match';
    errors.password = 'Password And Confirm Password don\'t match';
    hasErrors = true;
  }
  return hasErrors && errors;
}

const mapDispatchToProps = (dispatch) => {
  return {
    updateCustomerDetails: (values) => {
      return dispatch(updateProfile(values)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(updateProfileFailure(response.payload));
          var error = response.payload.data.error;
          throw new SubmissionError(error);
        } else {
          dispatch(updateProfileSuccess(response.payload));
        }
      });
    }
  }
}

var editCustomerProfile = reduxForm({
  form: 'EditCustomerProfile',
  fields: ['email', 'password', 'name'],
  validate
})


function mapStateToProps(state) {

  return {
    customer: state.customer && state.customer.customer ? state.customer.customer : null,
    customerProfile: state.customer ? state.customer.profile : null
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(editCustomerProfile(CustomerProfile));

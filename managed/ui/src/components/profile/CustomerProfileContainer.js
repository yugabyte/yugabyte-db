// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import {CustomerProfile} from '../profile';
import { reduxForm } from 'redux-form';
import {updateProfile, updateProfileSuccess, updateProfileFailure} from '../../actions/customers';
import {isValidObject} from '../../utils/ObjectUtils';
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
      dispatch(updateProfile(values)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(updateProfileFailure(response.payload));
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
  var data = {}
  const {customer} = state.customer;
  if (isValidObject(customer)) {
    data = {email: customer.email, name: customer.name}
  }
  return {
    customer: state.customer,
    initialValues: data,
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(editCustomerProfile(CustomerProfile));

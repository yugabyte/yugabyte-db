// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import {CustomerProfile} from '../profile';
import {updateProfile, updateProfileSuccess, updateProfileFailure} from '../../actions/customers';
import { SubmissionError } from 'redux-form';

const mapDispatchToProps = (dispatch) => {
  return {
    updateCustomerDetails: (values) => {
      return dispatch(updateProfile(values)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(updateProfileFailure(response.payload));
          const error = response.payload.data.error;
          throw new SubmissionError(error);
        } else {
          dispatch(updateProfileSuccess(response.payload));
        }
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer.currentCustomer.data,
    customerProfile: state.customer ? state.customer.profile : null
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(CustomerProfile);

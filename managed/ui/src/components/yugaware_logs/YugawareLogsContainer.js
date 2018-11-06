// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { YugawareLogs } from '../yugaware_logs';
import { reduxForm, SubmissionError } from 'redux-form';
import { getLogs, getLogsSuccess, getLogsFailure } from '../../actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    getLogs : () => {
      return dispatch(getLogs()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getLogsFailure(response.payload));
          const error = response.payload.data.error;
          throw new SubmissionError(error);
        } else {
          dispatch(getLogsSuccess(response.payload));
        }
      });
    }
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer
  };
}

function validate(values) {
  return false;
}

const getYugawareLogs = reduxForm({
  form: 'getYugawareLogs',
  fields: [],
  validate
});


export default connect(mapStateToProps, mapDispatchToProps)(getYugawareLogs(YugawareLogs));

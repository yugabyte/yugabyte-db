// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import clsx from 'clsx';
import { PageHeader } from 'react-bootstrap';
import { YBButton } from '../fields';
import { YBLabel } from '../../descriptors';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import YBLogo from '../../YBLogo/YBLogo';
import { browserHistory } from 'react-router';
import { Field, Form, Formik } from 'formik';
import * as Yup from 'yup';
import _ from 'lodash';
import { ROOT_URL, isSSOEnabled } from '../../../../config';
import { clearCredentials } from '../../../../routes';
import { trimString } from '../../../../utils/ObjectUtils';

class LoginForm extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showLoginFrom: false
    };
    clearCredentials();
  }

  componentDidMount = () => {
    this.props.getYugaWareVersion();
  };

  submitLogin = (formValues) => {
    const { loginCustomer } = this.props;
    formValues.email = trimString(formValues.email);
    loginCustomer(formValues);
  };

  componentDidUpdate(prevProps) {
    const {
      customer: { authToken, error }
    } = this.props;
    const currentAuth = prevProps.customer.authToken;
    if (getPromiseState(authToken).isSuccess() && !_.isEqual(authToken, currentAuth)) {
      if (error === 'Invalid') {
        this.props.resetCustomerError();
        browserHistory.goBack();
      } else {
        if (localStorage.getItem('__yb_intro_dialog__') !== 'hidden') {
          localStorage.setItem('__yb_intro_dialog__', 'new');
        }
        browserHistory.push('/');
      }
    }
  }

  runSSO() {
    const searchParam = new URLSearchParams(window.location.search);
    const pathToRedirect = searchParam.get('orig_url');
    if (localStorage.getItem('__yb_intro_dialog__') !== 'hidden') {
      localStorage.setItem('__yb_intro_dialog__', 'new');
    }
    window.location.replace(
      pathToRedirect
        ? `${ROOT_URL}/third_party_login?orig_url=${pathToRedirect}`
        : `${ROOT_URL}/third_party_login`
    );
  }

  render() {
    const {
      customer: { authToken, yugawareVersion }
    } = this.props;
    const version = getPromiseState(yugawareVersion).isSuccess()
      ? yugawareVersion.data?.version
      : null;

    const validationSchema = Yup.object().shape({
      email: Yup.string().required('Enter Email or Username'),

      password: Yup.string().required('Enter password')
    });

    const initialValues = {
      email: '',
      password: ''
    };

    const searchParam = new URLSearchParams(window.location.search);
    const user_not_found = searchParam.get('user_not_found');

    const showLoginFrom = !isSSOEnabled() || this.state.showLoginFrom;

    return (
      <div className="container full-height dark-background flex-vertical-middle">
        <div className="col-sm-5 dark-form login-form">
          <PageHeader bsClass="dark-form-heading">
            <YBLogo type="full" />
            <span>Admin Console</span>
          </PageHeader>

          {isSSOEnabled() && (
            <>
              <div className="divider-c">
                <div className="divider-ic divider"></div>
              </div>
              {user_not_found && !authToken.error && (
                <div
                  className={`alert alert-danger form-error-alert ${user_not_found ? '' : 'hide'}`}
                >
                  {
                    <strong>
                      {JSON.stringify(
                        `User not found: ${user_not_found}. Please contact administrator.`
                      )}
                    </strong>
                  }
                </div>
              )}
              <div>
                <YBButton
                  btnClass="btn btn-orange login-btns sso-btn"
                  btnText="Login with SSO"
                  onClick={this.runSSO}
                />
              </div>
              {!this.state.showLoginFrom && (
                <div>
                  <div
                    className="align-center link-text"
                    onClick={() => {
                      this.setState({ showLoginFrom: true });
                    }}
                  >
                    Super Admin Login
                  </div>
                </div>
              )}
            </>
          )}

          {showLoginFrom && (
            <Formik
              validationSchema={validationSchema}
              initialValues={initialValues}
              onSubmit={(values, { setSubmitting }) => {
                this.submitLogin(values);
                setSubmitting(false);
              }}
            >
              {({ handleSubmit, isSubmitting }) => (
                <Form onSubmit={handleSubmit} className={clsx(isSSOEnabled() && 'fade-in')}>
                  {isSSOEnabled() && (
                    <div className="align-center form-title">
                      Enter super admin credentials to login
                    </div>
                  )}
                  <div
                    className={`alert alert-danger form-error-alert ${
                      authToken.error ? '' : 'hide'
                    }`}
                  >
                    {<strong>{JSON.stringify(authToken.error)}</strong>}
                  </div>
                  <div className="clearfix login-fields">
                    <Field name="email">
                      {(props) => (
                        <YBLabel {...props} name="email">
                          <input
                            className="form-control login-input-box"
                            placeholder="Email or Username"
                            type="text"
                            {...props.field}
                          />
                        </YBLabel>
                      )}
                    </Field>
                    <Field name="password">
                      {(props) => (
                        <YBLabel {...props} name="password">
                          <input
                            className="form-control login-input-box"
                            placeholder="Password"
                            type="password"
                            {...props.field}
                          />
                        </YBLabel>
                      )}
                    </Field>
                  </div>
                  <div className="clearfix">
                    <YBButton
                      btnType="submit"
                      disabled={isSubmitting || getPromiseState(authToken).isLoading()}
                      btnClass={clsx(
                        'btn',
                        'login-btns',
                        isSSOEnabled() ? 'btn-default' : 'btn-orange'
                      )}
                      btnText="Login"
                    />
                  </div>
                </Form>
              )}
            </Formik>
          )}
          {version && (
            <span className="align-center yba-version"> Platform Version: {version}</span>
          )}
        </div>
      </div>
    );
  }
}

export default LoginForm;

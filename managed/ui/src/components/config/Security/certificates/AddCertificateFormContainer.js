// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { AddCertificateForm } from './';

import {
  addCertificate,
  addCertificateResponse,
  getTlsCertificates,
  getTlsCertificatesResponse,
  addCertificateReset,
  updateCertificate,
  updateCertificateResponse
} from '../../../../actions/customers';
import { closeDialog } from '../../../../actions/modal';
import { toast } from 'react-toastify';
import { handleCACertErrMsg } from '../../../customCACerts';

const mapDispatchToProps = (dispatch) => {
  return {
    addCertificate: (certificate, setSubmitting) => {
      dispatch(addCertificate(certificate)).then((response) => {
        dispatch(addCertificateResponse(response.payload));
        setSubmitting(false);
        if (!response.error) {
          dispatch(closeDialog());
          // fetch new certificates list

          dispatch(getTlsCertificates()).then((response) => {
            dispatch(getTlsCertificatesResponse(response.payload));
          });
        } else {
          if(handleCACertErrMsg(response.payload)){
            return;
          }
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(`Certificate adding has been failed:  ${errorMessage}`);
        }
      });
    },
    updateCertificate: (certID, certificate, setSubmitting) => {
      dispatch(updateCertificate(certID, certificate)).then((response) => {
        dispatch(updateCertificateResponse(response.payload));
        setSubmitting(false);
        if (!response.error) {
          dispatch(closeDialog());
          // fetch new certificates list

          dispatch(getTlsCertificates()).then((response) => {
            dispatch(getTlsCertificatesResponse(response.payload));
          });
        } else {
          if(handleCACertErrMsg(response.payload)){
            return;
          }
          const errorMessage = response.payload?.response?.data?.error || response.payload.message;
          toast.error(`Certificate updation failed:  ${errorMessage}`);
        }
      });
    },
    addCertificateReset: () => {
      dispatch(addCertificateReset());
    }
  };
};

function mapStateToProps(state, ownProps) {
  const { customer } = state;

  return {
    customer: customer
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(AddCertificateForm);

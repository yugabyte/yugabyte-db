// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import Certificates from './Certificates';
import { openDialog, closeDialog } from '../../actions/modal';

import { getTlsCertificates,
  getTlsCertificatesResponse }
  from 'actions/customers';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchCustomerCertificates: () => {
      dispatch(getTlsCertificates()).then((response) => {
        dispatch(getTlsCertificatesResponse(response.payload));
      });
    },
    showAddCertificateModal: () => {
      dispatch(openDialog("addCertificateModal"));
    },
    closeModal: () => {
      dispatch(closeDialog());
    },
  };
};

function mapStateToProps(state) {
  return {
    refreshReleases: state.customer.refreshReleases,
    customer: state.customer,
    modal: state.modal
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(Certificates);

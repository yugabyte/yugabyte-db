// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import UsersList from './UsersList';
import {
  changeUserRole,
  createUser,
  createUserFailure,
  createUserSuccess,
  deleteUser,
  deleteUserResponse,
  getCustomerUsers,
  getCustomerUsersFailure,
  getCustomerUsersSuccess,
  fetchPasswordPolicy,
  fetchPasswordPolicyResponse
} from '../../../actions/customers';
import { openDialog, closeDialog } from '../../../actions/modal';
import { toast } from 'react-toastify';
import { createErrorMessage } from '../../../utils/ObjectUtils';

const mapDispatchToProps = (dispatch) => {
  return {
    getCustomerUsers: () => {
      dispatch(getCustomerUsers()).then((response) => {
        try {
          if (response.payload.status !== 200) {
            dispatch(getCustomerUsersFailure(response.payload));
          } else {
            dispatch(getCustomerUsersSuccess(response.payload));
          }
        } catch (error) {
          console.error('Error while fetching customer users');
        }
      });
    },
    validateRegistration: () => {
      dispatch(fetchPasswordPolicy()).then((response) => {
        if (response.payload.status === 200) {
          dispatch(fetchPasswordPolicyResponse(response.payload));
        }
      });
    },
    createUser: (payload) => {
      return dispatch(createUser(payload)).then((response) => {
        try {
          if (response.payload.isAxiosError || response.payload.status !== 200) {
            toast.error(createErrorMessage(response.payload));
            return dispatch(createUserFailure(response.payload));
          } else {
            toast.success('User created successfully');
            return dispatch(createUserSuccess(response.payload));
          }
        } catch (error) {
          console.error('Error while creating customer users');
        }
      });
    },
    changeUserRole: (userUUID, newRole) => {
      return dispatch(changeUserRole(userUUID, newRole));
    },
    deleteUser: (userUUID) => {
      return dispatch(deleteUser(userUUID)).then((response) => {
        return dispatch(deleteUserResponse(response.payload));
      });
    },
    showAddUserModal: () => {
      dispatch(openDialog('addUserModal'));
    },
    showEditRoleModal: () => {
      dispatch(openDialog('editRoleModal'));
    },
    showConfirmDeleteModal: () => {
      dispatch(openDialog('deleteUserModal'));
    },
    closeModal: () => {
      dispatch(closeDialog());
    }
  };
};

function mapStateToProps(state) {
  return {
    customer: state.customer.currentCustomer,
    users: state.customer.users,
    modal: state.modal,
    passwordValidationInfo: state.customer.passwordValidationInfo
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UsersList);

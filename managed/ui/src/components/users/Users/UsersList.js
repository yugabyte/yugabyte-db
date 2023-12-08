// Copyright (c) YugaByte, Inc.

import { useEffect, useState } from 'react';
import { browserHistory } from 'react-router';
import { Row, Col, DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import { YBButton } from '../../common/forms/fields';

import { AddUserModal } from './modals/AddUserModal';
import { EditRoleModal } from './modals/EditRoleModal';
import { YBConfirmModal } from '../../modals';
import { isNonAvailable, isNotHidden, isDisabled } from '../../../utils/LayoutUtils';
import { timeFormatter } from '../../../utils/TableFormatters';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

export const UsersList = (props) => {
  const [userForModal, setUserModal] = useState(null);
  const {
    users,
    customer,
    modal: { visibleModal, showModal },
    showAddUserModal,
    closeModal,
    validateRegistration,
    getCustomerUsers,
    showConfirmDeleteModal,
    showEditRoleModal,
    deleteUser,
    passwordValidationInfo,
    changeUserRole,
    createUser
  } = props;

  useEffect(() => {
    getCustomerUsers();
    validateRegistration();
    if (isNonAvailable(customer.features, 'main.profile')) browserHistory.push('/');
  }, [customer.features, getCustomerUsers, validateRegistration]);

  const showDeleteUserModal = (user) => {
    setUserModal(user);
    showConfirmDeleteModal();
  };

  const editRole = (user) => {
    setUserModal(user);
    showEditRoleModal();
  };

  const doDeleteUser = async () => {
    const user = userForModal;
    try {
      await deleteUser(user.uuid);
    } catch (error) {
      console.error('Failed to delete user', error);
    } finally {
      getCustomerUsers();
      closeModal();
    }
  };

  const actionsDropdown = (user) => {
    const loginUserId = localStorage.getItem('userId');
    if (
      user.uuid === loginUserId ||
      isDisabled(customer.data.features, 'universe.create') ||
      user.role === 'SuperAdmin'
    ) {
      return null;
    } else {
      const disableRoleEdit = user?.ldapSpecifiedRole;; //if user is LDAP
      return (
        <DropdownButton
          className="btn btn-default"
          title="Actions"
          id="bg-nested-dropdown"
          pullRight
        >
          <MenuItem disabled={disableRoleEdit} onClick={() => !disableRoleEdit && editRole(user)}>
            <span className="fa fa-edit" /> Edit User Role
          </MenuItem>
          <MenuItem onClick={() => showDeleteUserModal(user)}>
            <span className="fa fa-trash" /> Delete User
          </MenuItem>
        </DropdownButton>
      );
    }
  };

  if (getPromiseState(users).isLoading() || getPromiseState(users).isInit()) {
    return <YBLoading />;
  }

  return (
    <div className="users-list-container">
      <Row className="header">
        {users?.data?.length > 0 && (
          <Col className="header-col">
            <h5>{users.data.length} Users</h5>
          </Col>
        )}
        <Col className="header-col justify-end">
          {isNotHidden(customer.data.features, 'universe.create') && (
            <RbacValidator
              isControl
              accessRequiredOn={ApiPermissionMap.CREATE_USER}
            >
              <YBButton
                btnClass="add-user-btn btn btn-lg btn-orange"
                onClick={showAddUserModal}
                btnText="Add User"
                btnIcon="fa fa-plus"
                disabled={isDisabled(customer.data.features, 'universe.create')}
              />
            </RbacValidator>
          )}
          {/* re-mount modals in order to internal forms show valid initial values all the times */}
          {showModal && visibleModal === 'addUserModal' && (
            <AddUserModal
              modalVisible
              onHide={closeModal}
              createUser={createUser}
              getCustomerUsers={getCustomerUsers}
              passwordValidationInfo={passwordValidationInfo}
            />
          )}
          {showModal && visibleModal === 'editRoleModal' && (
            <EditRoleModal
              modalVisible
              onHide={closeModal}
              user={userForModal}
              changeUserRole={changeUserRole}
              getCustomerUsers={getCustomerUsers}
            />
          )}
          <YBConfirmModal
            name="deleteUserModal"
            title="Delete User"
            hideConfirmModal={closeModal}
            currentModal="deleteUserModal"
            visibleModal={visibleModal}
            onConfirm={doDeleteUser}
            confirmLabel="Delete"
            cancelLabel="Cancel"
          >
            Are you sure you want to delete user <strong>{userForModal?.email}</strong> ?
          </YBConfirmModal>
        </Col>
      </Row>
      <Row>
        <YBPanelItem
          body={
            <BootstrapTable
              data={users.data}
              bodyStyle={{ marginBottom: '1%', paddingBottom: '1%' }}
              pagination
            >
              <TableHeaderColumn dataField="uuid" isKey hidden />
              <TableHeaderColumn dataField="email">Email</TableHeaderColumn>
              <TableHeaderColumn dataField="role">Role</TableHeaderColumn>
              <TableHeaderColumn dataField="creationDate" dataFormat={timeFormatter}>
                Created At
              </TableHeaderColumn>
              <TableHeaderColumn
                columnClassName="table-actions-cell"
                dataFormat={(cell, row) => actionsDropdown(row)}
              >
                Actions
              </TableHeaderColumn>
            </BootstrapTable>
          }
        />
      </Row>
    </div>
  );
};

export default UsersList;

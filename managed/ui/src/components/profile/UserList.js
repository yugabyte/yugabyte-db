// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, ButtonGroup, DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../panels';
import { connect } from 'react-redux';
import { getPromiseState } from '../../utils/PromiseUtils';
import { YBLoading } from '../common/indicators';
import { YBButton } from '../../components/common/forms/fields';
import { openDialog, closeDialog } from '../../actions/modal';
import { AddUserModal } from '../profile';
import {YBConfirmModal} from '../modals';
import { isNotHidden, isDisabled } from '../../utils/LayoutUtils';
import { getCustomerUsers, getCustomerUsersSuccess, getCustomerUsersFailure,
         deleteUser, deleteUserResponse } from '../../actions/customers';

class UserList extends Component {
  constructor(props) {
    super(props);
    this.state = {userToBeDeleted: {}};
  }

  componentDidMount() {
    this.props.getCustomerUsers();
  }

  showDeleteUserModal = (user) => {
    this.setState({userToBeDeleted: user});
    this.props.showConfirmDeleteModal();
  }

  deleteUser = () => {
    const user = this.state.userToBeDeleted;
    this.props.deleteUser(user.uuid);
    this.props.getCustomerUsers();
    this.props.closeModal();
  }
  render() {
    const { users, customer,
            modal: {visibleModal, showModal},
            showAddUserModal, closeModal } = this.props;
    const loginUserId = localStorage.getItem('userId');
    if (getPromiseState(users).isLoading() || getPromiseState(users).isInit()) {
      return <YBLoading />;
    }
    const tableBodyContainer = {marginBottom: "1%", paddingBottom: "1%"};
    const self = this;
    const formatActionButtons = function(item, row, disabled) {
      const {customer} = self.props;
      if (row.uuid !== loginUserId && !isDisabled(customer.data.features, "universe.create")) {
        return (
          <ButtonGroup>
            <DropdownButton className="btn btn-default"
              title="Actions" id="bg-nested-dropdown" pullRight>
              <MenuItem
                eventKey="1"
                className="menu-item-delete"
                onClick={self.showDeleteUserModal.bind(self, row)}>
                <span className="fa fa-trash"/>Delete User
              </MenuItem>
            </DropdownButton>
          </ButtonGroup>
        );
      }
    };


    return (
      <div className="user-list-container">
        <Row >
          <Col className="pull-left">
            <h2>Users</h2>
          </Col>
          <Col className="pull-right">
            {isNotHidden(customer.data.features, "universe.create") &&
              <YBButton btnClass="universe-button btn btn-lg btn-orange"
                onClick={showAddUserModal} btnText="Add User" btnIcon="fa fa-plus"
                disabled={isDisabled(customer.data.features, "universe.create")} />
            }
            <AddUserModal modalVisible={showModal && visibleModal === 'addUserModal'}
                          onHide={closeModal} />
            <YBConfirmModal name={"deleteUserModal"} title={"Delete User"}
                hideConfirmModal={closeModal} currentModal={"deleteUserModal"}
                visibleModal={visibleModal} onConfirm={this.deleteUser}
                confirmLabel={"Delete"} cancelLabel={"Cancel"}>
              {`Are you sure you want to delete user ${this.state.userToBeDeleted.email}`}
            </YBConfirmModal>
          </Col>
        </Row>
        <Row>
          <YBPanelItem
            body={
              <BootstrapTable data={users.data} bodyStyle={tableBodyContainer}>
                <TableHeaderColumn dataField="uuid" isKey={true} hidden={true}/>
                <TableHeaderColumn dataField="email" >
                  Email
                </TableHeaderColumn>
                <TableHeaderColumn dataField="role" >
                  Role
                </TableHeaderColumn>

                <TableHeaderColumn dataField="creationDate"  >
                  Created At
                </TableHeaderColumn>
                <TableHeaderColumn dataField={"actions"}
                  columnClassName={"table-actions-cell"} dataFormat={formatActionButtons}>
                  Actions
                </TableHeaderColumn>
              </BootstrapTable>
            }
          />
        </Row>
      </div>
    );
  }
}

const mapDispatchToProps = (dispatch) => {
  return {
    getCustomerUsers: () => {
      dispatch(getCustomerUsers()).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(getCustomerUsersFailure(response.payload));
        } else {
          dispatch(getCustomerUsersSuccess(response.payload));
        }
      });
    },
    deleteUser: (userUUID) => {
      dispatch(deleteUser(userUUID)).then((response) => {
        dispatch(deleteUserResponse(response.payload));
      });
    },
    showAddUserModal: () => {
      dispatch(openDialog("addUserModal"));
    },
    showConfirmDeleteModal: () => {
      dispatch(openDialog("deleteUserModal"));
    },
    closeModal: () => {
      dispatch(closeDialog());
    },
  };
};

function mapStateToProps(state, ownProps) {
  return {
    users: state.customer.users,
    modal: state.modal
  };
}


export default connect(mapStateToProps, mapDispatchToProps)(UserList);

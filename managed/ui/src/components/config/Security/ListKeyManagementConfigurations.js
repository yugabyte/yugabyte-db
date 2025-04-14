// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import { find } from 'lodash';
import { Button, DropdownButton, MenuItem } from 'react-bootstrap';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../panels';
import { ConfigDetails } from './ConfigDetails';
import { AssociatedUniverse } from '../../common/associatedUniverse/AssociatedUniverse';
import { DeleteKMSConfig } from './DeleteKMSConfig.tsx';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import {
  isRbacEnabled,
  userhavePermInRoleBindings
} from '../../../redesign/features/rbac/common/RbacUtils';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { Action, Resource } from '../../../redesign/features/rbac';
import { KmsProvider } from './KeyManagementConfiguration';
import { YBTooltip } from '../../../redesign/components';

export class ListKeyManagementConfigurations extends Component {
  state = {
    associatedUniverses: [],
    isVisibleModal: false,
    deleteConfig: null,
    configDetail: null
  };

  actionList = (item, row) => {
    const { in_use: inUse, universeDetails, provider: kmsProvider } = row.metadata;
    const { isAdmin, onEdit, isCipherTrustKmsEnabled } = this.props;

    const isRestrictedKmsProvider =
      kmsProvider === KmsProvider.CIPHERTRUST && !isCipherTrustKmsEnabled;
    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        <MenuItem
          onClick={() => {
            this.setState({ configDetail: row });
          }}
          data-testid="EAR-Details"
        >
          <i className="fa fa-info-circle"></i> Details
        </MenuItem>
        {(isAdmin || isRbacEnabled()) && (
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.MODIFY_CERTIFICATE}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <YBTooltip
              title={
                isRestrictedKmsProvider
                  ? 'Enable the runtime configuration yb.kms.allow_ciphertrust to edit this KMS configuration.'
                  : ''
              }
              placement="top"
            >
              <MenuItem
                onSelect={() => {
                  onEdit(row);
                }}
                data-testid="EAR-EditConfiguration"
                disabled={isRestrictedKmsProvider}
              >
                <i className="fa fa-pencil"></i> Edit Configuration
              </MenuItem>
            </YBTooltip>
          </RbacValidator>
        )}
        <RbacValidator
          accessRequiredOn={ApiPermissionMap.DELETE_CERTIFICATE}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <YBTooltip
            title={
              isRestrictedKmsProvider
                ? 'Enable the runtime configuration `yb.kms.allow_ciphertrust` to delete this KMS configuration.'
                : inUse
                ? 'The configuration is in use.'
                : ''
            }
            placement="top"
          >
            <MenuItem
              title={'Delete provider'}
              disabled={inUse || isRestrictedKmsProvider}
              onSelect={() => {
                this.setState({ deleteConfig: row });
              }}
              data-testid="EAR-DeleteConfiguration"
            >
              <i className="fa fa-trash"></i> Delete Configuration
            </MenuItem>
          </YBTooltip>
        </RbacValidator>

        <RbacValidator
          customValidateFunction={() => userhavePermInRoleBindings(Resource.UNIVERSE, Action.READ)}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            onClick={() => {
              this.setState({ associatedUniverses: [...universeDetails], isVisibleModal: true });
            }}
            data-testid="EAR-ShowUniverses"
          >
            <i className="fa fa-eye"></i> Show Universes
          </MenuItem>
        </RbacValidator>
      </DropdownButton>
    );
  };

  /**
   * Close the modal by setting the local flag
   */
  closeModal = () => {
    this.setState({ isVisibleModal: false });
  };

  hideConfigDetail = () => {
    this.setState({ configDetail: null });
  };

  handleDeleteConfig = (config) => {
    this.props.onDelete(config.metadata.configUUID);
    this.setState({ deleteConfig: null });
  };

  hideDeleteConfig = () => {
    this.setState({ deleteConfig: null });
  };

  render() {
    const { configs, onCreate } = this.props;

    const { associatedUniverses, isVisibleModal, configDetail, deleteConfig } = this.state;

    const showConfigProperties = (item, row) => {
      return (
        <div>
          <a
            className="show_details"
            onClick={(e) => {
              this.setState({ configDetail: row });
              e.preventDefault();
            }}
            href="/"
          >
            Show details
          </a>
        </div>
      );
    };

    return (
      <div>
        <YBPanelItem
          header={
            <Fragment>
              <h2 className="table-container-title pull-left">List Configurations</h2>
              <FlexContainer className="pull-right">
                <FlexShrink>
                  <RbacValidator accessRequiredOn={ApiPermissionMap.CREATE_CERTIFICATE} isControl>
                    <Button bsClass="btn btn-orange btn-config" onClick={onCreate}>
                      Create New Config
                    </Button>
                  </RbacValidator>
                </FlexShrink>
              </FlexContainer>
            </Fragment>
          }
          body={
            <Fragment>
              <BootstrapTable data={configs} className="backup-list-table middle-aligned-table">
                <TableHeaderColumn
                  dataField="metadata"
                  dataFormat={(cell) => cell.configUUID}
                  isKey={true}
                  hidden={true}
                />
                <TableHeaderColumn
                  dataField="metadata"
                  dataFormat={(cell) => cell.name}
                  dataSort
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Name
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="metadata"
                  dataFormat={(cell) => cell.provider}
                  dataSort
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Provider
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="base_url"
                  dataFormat={showConfigProperties}
                  columnClassName="no-border name-column"
                  className="no-border"
                >
                  Properties
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="configActions"
                  dataFormat={this.actionList}
                  width="120px"
                  columnClassName="yb-actions-cell"
                />
              </BootstrapTable>
              <AssociatedUniverse
                visible={isVisibleModal}
                onHide={this.closeModal}
                associatedUniverses={associatedUniverses}
                title="KMS Provider"
              />
            </Fragment>
          }
          noBackground
        />
        {configDetail && (
          <ConfigDetails
            visible={!!configDetail}
            onHide={this.hideConfigDetail}
            data={configDetail}
          />
        )}

        {deleteConfig && (
          <DeleteKMSConfig
            open={!!deleteConfig}
            onHide={this.hideDeleteConfig}
            onSubmit={this.handleDeleteConfig}
            config={deleteConfig}
          />
        )}
      </div>
    );
  }
}

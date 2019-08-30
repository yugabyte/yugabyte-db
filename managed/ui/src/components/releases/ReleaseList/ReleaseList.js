// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { DropdownButton } from 'react-bootstrap';

import { YBPanelItem } from 'components/panels';
import { YBButton } from 'components/common/forms/fields';
import { TableAction } from 'components/tables';
import { YBLoadingCircleIcon } from 'components/common/indicators';
import { getPromiseState } from 'utils/PromiseUtils';

import { showOrRedirect } from 'utils/LayoutUtils';

import './ReleaseList.scss';

export default class ReleaseList extends Component {
  static defaultProps = {
    title : "Releases"
  }

  componentWillMount() {
    this.props.getYugaByteReleases();
  }

  refreshRelease = () => {
    this.props.refreshYugaByteReleases();
    this.props.getYugaByteReleases();
  }

  onModalSubmit = () => {
    this.props.getYugaByteReleases();
  }

  render() {
    const { releases, title, customer: { currentCustomer }} = this.props;
    showOrRedirect(currentCustomer.data.features, "main.releases");

    if (getPromiseState(releases).isLoading() ||
        getPromiseState(releases).isInit()) {
      return <YBLoadingCircleIcon size="medium" />;
    }
    const releaseInfos = Object.keys(releases.data).map((version) => {
      const releaseInfo = releases.data[version];
      releaseInfo.version = version;
      return releaseInfo;
    });

    const rowClassNameFormat = function(row, rowIdx) {
      return 'td-column-' + row.state.toLowerCase();
    };
    const self = this;

    const formatActionButtons = function(item, row) {
      let allowedActions = null;
      switch(item) {
        case "ACTIVE":
          allowedActions = ["DISABLE", "DELETE"];
          break;
        case "DISABLED":
          allowedActions = ["DELETE", "ACTIVE"];
          break;
        default:
          break;
      }
      if (!allowedActions) {
        return;
      }

      return (
        <DropdownButton className="btn btn-default"
          title="Actions" id="bg-nested-dropdown" pullRight>
          {allowedActions.map((action, idx) => {
            const actionType = action.toLowerCase() + "-release";
            return (<TableAction key={action + "-" + idx} currentRow={row} actionType={actionType}
                     onModalSubmit={self.onModalSubmit} />);
          })}

        </DropdownButton>
      );
    };

    return (
      <YBPanelItem
        header={
          <div>
            <div className='pull-right'>
              <div className="release-list-action-btn-group">
                <YBButton btnText={"Refresh"} btnIcon={"fa fa-refresh"}
                  btnClass={'btn btn-primary'} onClick={this.refreshRelease} />
                <TableAction className="table-action" btnClass={"btn-default"}
                  actionType="import-release" isMenuItem={false}
                  onModalSubmit={self.onModalSubmit} />
              </div>
            </div>
            <h2 className='content-title'>{title}</h2>
          </div>
        }
        body={
          <BootstrapTable data={releaseInfos} className={"release-list-table"}
            trClassName={rowClassNameFormat} pagination={true}>
            <TableHeaderColumn dataField="version" isKey={true} width='100'
                              columnClassName="no-border name-column" className="no-border">
              Version
            </TableHeaderColumn>
            <TableHeaderColumn dataField="filePath" tdStyle={ { whiteSpace: 'normal' } }
                              columnClassName="no-border name-column" className="no-border">
              File Path
            </TableHeaderColumn>
            <TableHeaderColumn dataField="imageTag" tdStyle={ { whiteSpace: 'normal' } }
                              columnClassName="no-border " className="no-border">
              Registry Path
            </TableHeaderColumn>
            <TableHeaderColumn dataField="notes" tdStyle={ { whiteSpace: 'normal' } }
                              columnClassName="no-border name-column" className="no-border">
              Release Notes
            </TableHeaderColumn>
            <TableHeaderColumn dataField="state" width='150'
                              columnClassName="no-border name-column" className="no-border">
              State
            </TableHeaderColumn>
            <TableHeaderColumn dataField="state" dataFormat={ formatActionButtons }
                              columnClassName={"yb-actions-cell"} className="no-border">
              Actions
            </TableHeaderColumn>

          </BootstrapTable>
        }
      />
    );
  }
}

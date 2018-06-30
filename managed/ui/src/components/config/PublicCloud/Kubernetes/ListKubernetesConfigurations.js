// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { Button } from 'react-bootstrap';
import { FlexContainer, FlexShrink, FlexGrow } from '../../../common/flexbox/YBFlexBox';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../../panels';
import { YBCopyButton } from '../../../common/descriptors';
import { KUBERNETES_PROVIDERS } from 'config';

export default class ListKubernetesConfigurations extends Component {
  static propTypes  = {
    providers: PropTypes.array.isRequired,
    onCreate: PropTypes.func.isRequired,
    type: PropTypes.string.isRequired
  }

  render() {
    const { providers, type } = this.props;

    const formatConfigPath = function(item, row) {
      return (
        <FlexContainer>
          <FlexGrow style={{width: '75%', overflow: 'hidden', textOverflow: 'ellipsis'}}>
            {row.configPath}
          </FlexGrow>
          <FlexGrow>
            <YBCopyButton text={row.configPath}/>
          </FlexGrow>
        </FlexContainer>
      );
    };
    const providerTypeMetadata = KUBERNETES_PROVIDERS.find((providerType) => providerType.code === type);

    return (
      <YBPanelItem
        header={
          <Fragment>
            <h2 className="table-container-title pull-left">{providerTypeMetadata.name} configs</h2>
            <FlexContainer className="pull-right">
              <FlexShrink>
                <Button bsClass="btn btn-orange" onClick={this.props.onCreate}>
                  Create Config
                </Button>
              </FlexShrink>
            </FlexContainer>
          </Fragment>

        }
        body={
          <BootstrapTable data={providers} pagination={true} className="backup-list-table">
            <TableHeaderColumn dataField="uuid" isKey={true} hidden={true}/>
            <TableHeaderColumn dataField="name" dataSort
                              columnClassName="no-border name-column" className="no-border">
              Name
            </TableHeaderColumn>
            <TableHeaderColumn dataField="type" dataSort
                              columnClassName="no-border name-column" className="no-border">
              Provider Type
            </TableHeaderColumn>
            <TableHeaderColumn dataField="region" dataSort
                              columnClassName="no-border name-column" className="no-border">
              Region
            </TableHeaderColumn>
            <TableHeaderColumn dataField="zones" dataSort
                              columnClassName="no-border name-column" className="no-border">
              Zones
            </TableHeaderColumn>
            <TableHeaderColumn dataField="configPath" dataSort dataFormat={formatConfigPath}
                              columnClassName="no-border name-column" className="no-border">
              Config Path
            </TableHeaderColumn>
          </BootstrapTable>
        }
      />
    );
  }

}

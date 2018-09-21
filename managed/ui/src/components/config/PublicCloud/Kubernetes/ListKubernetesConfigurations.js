// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { Button } from 'react-bootstrap';
import { FlexContainer, FlexShrink, FlexGrow } from '../../../common/flexbox/YBFlexBox';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBPanelItem } from '../../../panels';
import { Row, Col } from 'react-bootstrap';
import { Link, browserHistory } from 'react-router';
import { YBCopyButton } from '../../../common/descriptors';
import { KUBERNETES_PROVIDERS } from 'config';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';
import { YBTextInput, YBModal } from '../../../common/forms/fields';

export default class ListKubernetesConfigurations extends Component {
  static propTypes  = {
    providers: PropTypes.array.isRequired,
    onCreate: PropTypes.func.isRequired,
    type: PropTypes.string.isRequired
  }

  render() {
    const { 
      providers, 
      deleteProviderConfig,
      activeProviderUUID,
      type 
    } = this.props;

    const providerLinkFormatter = function(cell, row) {
      return <Link to={`/config/cloud/${type}/${row.uuid}`}>{cell}</Link>;
    };

    const providerDetails = providers.find(item => {
      if(item.uuid === activeProviderUUID) return item;
      return false;
    });

    const self = this;
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
    const actionList = function(item, row) {
      return (
        <Button bsClass="btn btn-default btn-config pull-right" onClick={deleteProviderConfig.bind(self, row.uuid)}>
          Delete Configuration
        </Button>
      );
    };
    const providerTypeMetadata = KUBERNETES_PROVIDERS.find((providerType) => providerType.code === type);

    const onModalHide = () => {
      const { type } = this.props;
      browserHistory.push(`/config/cloud/${type}`);
    };
    
    return (
      <div>
        <YBPanelItem
          header={
            <Fragment>
              <h2 className="table-container-title pull-left">{providerTypeMetadata.name} configs</h2>
              <FlexContainer className="pull-right">
                <FlexShrink>
                  <Button bsClass="btn btn-orange btn-config" onClick={this.props.onCreate}>
                    Create Config
                  </Button>
                </FlexShrink>
              </FlexContainer>
            </Fragment>

          }
          body={
            <BootstrapTable data={providers} pagination={true} className="backup-list-table middle-aligned-table">
              <TableHeaderColumn dataField="uuid" isKey={true} hidden={true}/>
              <TableHeaderColumn dataField="name" dataSort dataFormat={providerLinkFormatter}
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
              <TableHeaderColumn dataField="configActions" dataFormat={actionList}
                                columnClassName="no-border name-column no-side-padding" className="no-border">
              </TableHeaderColumn>
            </BootstrapTable>
          }
          noBackground
        />
        { isDefinedNotNull(activeProviderUUID) &&
          <YBModal visible={true} formName={"RollingUpgradeForm"}
            onHide={onModalHide} title={providerDetails.name + " provider info"}>
            {
              <div>
                <Row className="config-provider-row">
                  <Col lg={5}>
                    <div className="form-item-custom-label">Name</div>
                  </Col>
                  <Col lg={7}>
                    <YBTextInput label="Provider name:" isReadOnly={true} input={{value: providerDetails.name}} />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={5}>
                    <div className="form-item-custom-label">Type</div>
                  </Col>
                  <Col lg={7}>
                    <YBTextInput label="Provider name:" isReadOnly={true} input={{value: providerDetails.type}} />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={5}>
                    <div className="form-item-custom-label">Kube Config</div>
                  </Col>
                  <Col lg={7}>
                    <YBTextInput label="Kube Config Path:" isReadOnly={true} input={{value: providerDetails.configPath}} />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={5}>
                    <div className="form-item-custom-label">Service Account</div>
                  </Col>
                  <Col lg={7}>
                    <YBTextInput label="Service Account name:" isReadOnly={true} input={{value: providerDetails.serviceAccount}} />
                  </Col>
                </Row>

                <Row className="config-provider-row">
                  <Col lg={5}>
                    <div className="form-item-custom-label">Namespace</div>
                  </Col>
                  <Col lg={7}>
                    <YBTextInput label="Optional Yugaware Namespace:" isReadOnly={true} input={{value: providerDetails.namespace}} />
                  </Col>
                </Row>

                <Row className="config-provider-row">
                  <Col lg={5}>
                    <div className="form-item-custom-label">Region</div>
                  </Col>
                  <Col lg={7}>
                    <YBTextInput label="Region:" isReadOnly={true} input={{value: providerDetails.region}} />
                  </Col>
                </Row>
              </div>
            }
          </YBModal>
        }
      </div>
    );
  }

}

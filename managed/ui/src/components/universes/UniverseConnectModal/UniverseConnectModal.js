// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import axios from 'axios';
import 'react-bootstrap-table/css/react-bootstrap-table.css';
import { YBModal, YBButton } from '../../common/forms/fields';
import { getUniverseEndpoint } from 'actions/common';
import { connect } from 'react-redux';
import { openDialog, closeDialog } from '../../../actions/modal';
import { FlexContainer, FlexGrow } from '../../common/flexbox/YBFlexBox';
import { Link } from 'react-router';
import { getPromiseState } from 'utils/PromiseUtils';
import { isNonEmptyObject } from "../../../utils/ObjectUtils";
import { YBLoading } from '../../common/indicators';
import { YBCodeBlock } from '../../common/descriptors';
import { getPrimaryCluster } from '../../../utils/UniverseUtils';

import './UniverseConnectModal.scss';

class UniverseConnectModal extends Component {
  constructor(props) {
    super(props);
    this.state = {
      connectIp: null
    };
  }

  renderEndpointUrl = (endpointUrl, endpointName) => {
    return (
      <Link to={endpointUrl} target="_blank" rel="noopener noreferrer">
        <YBButton btnClass={"btn btn-default"} btnText={endpointName} btnIcon={"fa fa-external-link"} />
      </Link>
    );
  }

  componentDidMount = () => {
    const {
      universe: { currentUniverse }
    } = this.props;
    if (getPromiseState(currentUniverse).isSuccess() && isNonEmptyObject(currentUniverse.data)) {
      const universeInfo = currentUniverse.data;
      const universeUUID = universeInfo.universeUUID;

      const { universeDetails: { clusters }} = universeInfo;
      const primaryCluster = getPrimaryCluster(clusters);
      const userIntent = primaryCluster && primaryCluster.userIntent;
      
      // check if there's a Hosted Zone 
      if (userIntent.providerType === "aws" && universeInfo.dnsName) {
        this.setState({connectIp: universeInfo.dnsName});
      } else {
        // if no go to YCQL endpoint and fetch IPs
        const ycqlServiceUrl = getUniverseEndpoint(universeUUID) + "/yqlservers";
        axios.get(ycqlServiceUrl)
          .then((response) => this.setState({connectIp: response.data.split(',')[0].trim().split(':')[0]}))
          .catch(() => console.log("YCQL endpoint is unavailable"));
      }
    }
  }

  render() {
    const { 
      showOverviewConnectModal,
      closeModal,
      modal: { showModal, visibleModal },
      universe: { currentUniverse }
    } = this.props;

    let content = null;
    if (getPromiseState(currentUniverse).isLoading() || getPromiseState(currentUniverse).isInit()) {
      content = <YBLoading />;
    }
    if (getPromiseState(currentUniverse).isSuccess() && isNonEmptyObject(currentUniverse.data)) {
      const universeInfo = currentUniverse.data;
      const { universeDetails: { clusters }} = universeInfo;
      const primaryCluster = getPrimaryCluster(clusters);
      const userIntent = primaryCluster && primaryCluster.userIntent;
      const universeId = universeInfo.universeUUID;

      const ycqlServiceUrl = getUniverseEndpoint(universeId) + "/yqlservers";
      const ysqlServiceUrl = getUniverseEndpoint(universeId) + "/ysqlservers";
      const yedisServiceUrl = getUniverseEndpoint(universeId) + "/redisservers";
      const endpointsContent = (<FlexContainer className="btn-group-cnt endpoint-buttons">
        <FlexGrow>{this.renderEndpointUrl(ycqlServiceUrl, "YCQL")}</FlexGrow>
        {userIntent.enableYSQL && <FlexGrow>{this.renderEndpointUrl(ysqlServiceUrl, "YSQL")}</FlexGrow>}
        <FlexGrow>{this.renderEndpointUrl(yedisServiceUrl, "YEDIS")}</FlexGrow>
      </FlexContainer>); 
      const connectIp = this.state.connectIp;
      content = (<Fragment>
        <h4>Services</h4>
        <YBCodeBlock>
          JDBC : postgresql://postgres@{connectIp}:5433<br/>
          YSQL : ./bin/psql -U postgres -h {connectIp} -p 5433<br/>
          YCQL : ./bin/cqlsh {connectIp} 9042<br/>
          YEDIS : ./bin/redis-cli -h {connectIp} -p 6379<br/>
          Web UI : http://{connectIp}:7000/
        </YBCodeBlock>
        <h4>Endpoints</h4>
        {endpointsContent}
      </Fragment>);
    }
    return (
      <Fragment>
        <YBButton btnText={"Connect"} btnClass={"btn btn-orange"} onClick={showOverviewConnectModal}/>
        <YBModal title={"Connect"}
          visible={showModal && visibleModal === "UniverseConnectModal"}
          onHide={closeModal}
          showCancelButton={true}
          cancelLabel={"Close"}>
          {content}
        </YBModal>
      </Fragment>
    );
  }
}

const mapDispatchToProps = (dispatch) => {
  return {
    showOverviewConnectModal: () => {
      dispatch(openDialog("UniverseConnectModal"));
    },
    closeModal: () => {
      dispatch(closeDialog());
    },
  };
};

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer,
    layout: state.customer.layout,
    universe: state.universe,
    modal: state.modal
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseConnectModal);

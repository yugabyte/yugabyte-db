// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { FormattedDate } from 'react-intl';
import { ROOT_URL } from '../../../config';
import { DescriptionList, YBCopyButton } from '../../common/descriptors';
import { FlexContainer, FlexGrow, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { getPrimaryCluster } from "../../../utils/UniverseUtils";

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { universeInfo, universeInfo: {universeDetails: {clusters}}, currentCustomer } = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster && primaryCluster.userIntent;
    const universeId = universeInfo.universeUUID;
    const customerId = currentCustomer.data.uuid;
    const formattedCreationDate = (
      <FormattedDate value={universeInfo.creationDate} year='numeric' month='long' day='2-digit'
                     hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />
    );
    const universeIdData = <FlexContainer><FlexGrow style={{overflow: 'hidden', textOverflow: 'ellipsis'}}>{universeId}</FlexGrow><FlexShrink><YBCopyButton text={universeId}/></FlexShrink></FlexContainer>;
    const endpointUrl = ROOT_URL + "/customers/" + customerId +
                        "/universes/" + universeId + "/yqlservers";
    const endpoint = (
      <a href={endpointUrl} target="_blank" rel="noopener noreferrer">Endpoint &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>
    );
    const universeInfoItems = [
      {name: "Universe ID", data: universeIdData},
      {name: "Launch Time", data: formattedCreationDate},
      {name: "CQL Service", data: endpoint},
      {name: "YugaByte Version", data: userIntent.ybSoftwareVersion || 'n/a'},
    ];

    if (userIntent.providerType === "aws") {
      const dnsNameData = (
        <FlexContainer>
          <FlexGrow style={{overflow: 'hidden', textOverflow: 'ellipsis'}}>
            {universeInfo.dnsName}
          </FlexGrow>
          <FlexShrink>
            <YBCopyButton text={universeInfo.dnsName}/>
          </FlexShrink>
        </FlexContainer>);
      universeInfoItems.push({name: "Hosted Zone Name", data: dnsNameData });
    }

    return (
      <DescriptionList listItems={universeInfoItems} />
    );
  }
}

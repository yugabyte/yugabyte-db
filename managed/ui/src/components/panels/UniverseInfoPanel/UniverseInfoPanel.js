// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { FormattedDate } from 'react-intl';
import { getUniverseEndpoint } from 'actions/common';
import { DescriptionList, YBCopyButton } from '../../common/descriptors';
import { FlexContainer, FlexGrow, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { getPrimaryCluster } from "../../../utils/UniverseUtils";

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  renderEndpointUrl = (endpointUrl) => {
    return (
      <a href={endpointUrl} target="_blank" rel="noopener noreferrer">Endpoint &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>
    );
  }

  render() {
    const { universeInfo, universeInfo: {universeDetails: {clusters}} } = this.props;
    const primaryCluster = getPrimaryCluster(clusters);
    const userIntent = primaryCluster && primaryCluster.userIntent;
    const universeId = universeInfo.universeUUID;
    const formattedCreationDate = (
      <FormattedDate value={universeInfo.creationDate} year='numeric' month='long' day='2-digit'
                     hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />
    );
    const universeIdData = <FlexContainer><FlexGrow style={{overflow: 'hidden', textOverflow: 'ellipsis'}}>{universeId}</FlexGrow><FlexShrink><YBCopyButton text={universeId}/></FlexShrink></FlexContainer>;
    const ycqlServiceUrl = getUniverseEndpoint(universeId) + "/yqlservers";
    const yedisServiceUrl = getUniverseEndpoint(universeId) + "/redisservers";
    const universeInfoItems = [
      {name: "DB Version", data: userIntent.ybSoftwareVersion || 'n/a'},
      {name: "YCQL Service", data: this.renderEndpointUrl(ycqlServiceUrl)},
      {name: "YEDIS Service", data: this.renderEndpointUrl(yedisServiceUrl)},
      {name: "Universe ID", data: universeIdData},
      {name: "Launch Time", data: formattedCreationDate},
    ];

    if (userIntent.providerType === "aws") {
      const dnsNameData = (
        <FlexContainer>
          <FlexGrow style={{overflow: 'hidden', textOverflow: 'ellipsis'}}>
            {universeInfo.dnsName}
          </FlexGrow>
          <FlexGrow>
            <YBCopyButton text={universeInfo.dnsName}/>
          </FlexGrow>
        </FlexContainer>);
      universeInfoItems.push({name: "Hosted Zone Name", data: dnsNameData });
    }

    return (
      <DescriptionList listItems={universeInfoItems} />
    );
  }
}

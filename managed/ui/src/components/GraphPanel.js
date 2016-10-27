// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import InlineFrame from './InlineFrame';
import YBPanelItem from './YBPanelItem';

export default class GraphPanel extends Component {
  static propTypes = {
    nodePrefix: PropTypes.string.isRequired,
    panelIds: PropTypes.array
  };

  static defaultProps = {
    grafanaUrl: "http://10.9.67.55:3000/dashboard-solo/db/yugabyte-cluster",
    panelIds: [17, 18]
  }

  render() {
    const { nodePrefix, panelIds, grafanaUrl, graph: {graphFilter} } = this.props;
    var toTimestampMs = new Date().getTime();
    // By default we have a filter range of 1 hour ago.
    var fromTimestampMs = toTimestampMs - 60 * 60 * 1000;
    if (graphFilter !== null) {
      if (graphFilter.filterType === "hour") {
        fromTimestampMs = toTimestampMs - (60 * 60 * graphFilter.filterValue * 1000);
      }
    }

    const panelFrames = panelIds.map(function(panelId) {
      var panelUrl = grafanaUrl + "?panelId=" + panelId +
        "&from=" + fromTimestampMs + "&to=" + toTimestampMs +
        "&var-cluster=" + encodeURI(nodePrefix) + "&fullscreen&var-host=All";
      return (<InlineFrame key={panelId} src={panelUrl} className="graph-panel" />);
    });
    return (
      <YBPanelItem name="Graph Panels">
        {panelFrames}
      </YBPanelItem>
    );
  }
}

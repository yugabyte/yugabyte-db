import React, { FC, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { usePrevious } from 'react-use';
import _ from 'lodash';

import lightBulbIcon from '../images/lightbulb.svg';
import { CONST_VAR } from '../helpers/const';
import { CpuMeasureRecommendation } from '../../../redesign/helpers/dtos';
import './styles.scss';

var Plotly = require('plotly.js/lib/index-basic.js');

export const ConnectionSkew: FC<CpuMeasureRecommendation> = ({ data, summary }) => {
  const { t } = useTranslation();
  const previousData = usePrevious(data);
  const maxNodeConnections = {
    x: [data.maxNodeValue],
    y: [data.maxNodeName],
    // <extra></extra> removes the trace information on hover
    hovertemplate: `<i>Connections</i>: ${data.maxNodeValue} <extra></extra>`,
    showlegend: false,
    width: 0.2,
    orientation: 'h',
    type: 'bar'
  };

  const avgNodeConnections = {
    x: [data.otherNodesAvgValue],
    y: [CONST_VAR.AVG_NODES],
    hovertemplate: `<i>Connections</i>: ${data.otherNodesAvgValue} <extra></extra>`,
    showlegend: false,
    width: 0.2,
    orientation: 'h',
    type: 'bar'
  };

  useEffect(() => {
    if (
      !_.isEqual(previousData, data)
    ) {
      const chartData = [avgNodeConnections, maxNodeConnections];
      var layout = {
        showlegend: false,
        autosize: true,
        height: 170,
        barmode: 'group',
        margin: {
          l: 165,
          b: 30,
          t: 10,
        },
        hovermode: 'closest'
      };
      Plotly.newPlot('connectionsSkewGraph', chartData, layout, { displayModeBar: false });
    }
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <div>
      <div className="recommendationBox">
        <span> {summary} </span>
        <div className="recommendationAdvice">
          <img src={lightBulbIcon} alt="more" className="learnMoreImage" />
          <span className="learnPerfAdvisorText">
            {t('clusterDetail.performance.advisor.Recommendation')}
            {t('clusterDetail.performance.advisor.Separator')}
            {t('clusterDetail.performance.advisor.ReviewLoadBalancing')}
          </span>
        </div>
      </div>
      <span className="queryText">{t('clusterDetail.performance.chartTitle.Connections')}</span>
      <div id="connectionsSkewGraph" >
      </div>
    </div>
  )
}

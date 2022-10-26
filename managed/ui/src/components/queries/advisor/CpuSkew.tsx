import React, { FC, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { usePrevious } from 'react-use';
import _ from 'lodash';

import lightBulbIcon from '../images/lightbulb.svg';
import { EXTERNAL_LINKS, CONST_VAR } from '../helpers/const';
import { CpuMeasureRecommendation } from '../../../redesign/helpers/dtos';
import './styles.scss';

var Plotly = require('plotly.js/lib/index-basic.js');

export const CpuSkew: FC<CpuMeasureRecommendation> = ({ data, summary }) => {
  const { t } = useTranslation();
  const previousData = usePrevious(data);
  const maxNodeConnections = {
    x: [data.maxNodeValue],
    y: [data.maxNodeName],
    // <extra></extra> removes the trace information on hover
    hovertemplate: `${data.maxNodeValue}% <extra></extra>`,
    showlegend: false,
    width: 0.2,
    orientation: 'h',
    type: 'bar'
  }

  const avgNodeConnections = {
    x: [data.otherNodesAvgValue],
    y: [CONST_VAR.AVG_NODES],
    hovertemplate: `${data.otherNodesAvgValue}% <extra></extra>`,
    showlegend: false,
    width: 0.2,
    orientation: 'h',
    type: 'bar'
  }

  useEffect(() => {
    if (
      !_.isEqual(previousData, data)
    ) {
      const chartData = [avgNodeConnections, maxNodeConnections];
      var layout = {
        showlegend: false,
        height: 170,
        autosize: true,
        barmode: 'group',
        margin: {
          l: 165,
          b: 30,
          t: 10,
        },
        hovermode: 'closest'
      };
      Plotly.newPlot('cpuSkewGraph', chartData, layout, { displayModeBar: false });
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
            {t('clusterDetail.performance.advisor.RebalanceAndTroubleshoot')}
            <a
              target="_blank"
              rel="noopener noreferrer"
              className="learnSchemaSuggestion"
              href={EXTERNAL_LINKS.PERF_ADVISOR_DOCS_LINK}
            >
              {t('clusterDetail.performance.advisor.LearnHow')}
            </a>
          </span>
        </div>
      </div>
      <span className="queryText"> {t('clusterDetail.performance.chartTitle.CpuUsagePercentage')}</span>
      <div id="cpuSkewGraph" >
      </div>
    </div>
  )
}

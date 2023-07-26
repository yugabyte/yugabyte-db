import React, { FC, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { usePrevious } from 'react-use';
import _ from 'lodash';

import lightBulbIcon from '../images/lightbulb.svg';
import { EXTERNAL_LINKS, CONST_VAR } from '../helpers/constants';
import { PerfRecommendationProps } from '../../../redesign/utils/dtos';
import './styles.scss';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const Plotly = require('plotly.js/lib/index-basic.js');

export const HotShard: FC<PerfRecommendationProps> = ({ data, summary }) => {
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
  };

  const avgNodeConnections = {
    x: [data.otherNodesAvgValue],
    y: [CONST_VAR.AVG_NODES],
    hovertemplate: `${data.otherNodesAvgValue}% <extra></extra>`,
    showlegend: false,
    width: 0.2,
    orientation: 'h',
    type: 'bar'
  };

  useEffect(() => {
    if (!_.isEqual(previousData, data)) {
      const chartData = [avgNodeConnections, maxNodeConnections];
      const layout = {
        showlegend: false,
        height: 170,
        autosize: true,
        barmode: 'group',
        margin: {
          l: 165,
          b: 30,
          t: 10,
          r: 50
        },
        yaxis: {
          automargin: true
        },
        hovermode: 'closest'
      };
      Plotly.newPlot('hotShardGraph', chartData, layout, { displayModeBar: false });
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
            {data.suggestion}
            <a
              target="_blank"
              rel="noopener noreferrer"
              className="learnRecommendationSuggestions"
              href={EXTERNAL_LINKS.HOT_SHARD}
            >
              {t('clusterDetail.performance.advisor.LearnHow')}
            </a>
          </span>
        </div>
      </div>
      <div className="chartBox">
        <span className="queryText">{t('clusterDetail.performance.chartTitle.HotShardCount')}</span>
        <div id="hotShardGraph"></div>
      </div>
    </div>
  );
};

import { FC, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { usePrevious } from 'react-use';
import _ from 'lodash';

import lightBulbIcon from '../images/lightbulb.svg';
import { EXTERNAL_LINKS, CONST_VAR } from '../helpers/constants';
import { QueryLoadRecommendation } from '../../../redesign/utils/dtos';
import './styles.scss';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const Plotly = require('plotly.js/lib/index-basic.js');

export const QueryLoadSkew: FC<QueryLoadRecommendation> = ({ data, summary }) => {
  const previousData = usePrevious(data);
  const { t } = useTranslation();
  const maxNodeData = {
    x: ['Select', 'Insert', 'Update', 'Delete'],
    y: [
      data.maxNodeDistribution.numSelect,
      data.maxNodeDistribution.numInsert,
      data.maxNodeDistribution.numUpdate,
      data.maxNodeDistribution.numDelete
    ],
    // <extra></extra> removes the trace information on hover
    hovertemplate: '%{y}<extra></extra>',
    width: 0.2,
    type: 'bar',
    name: data.maxNodeName
  };

  const otherNodeData = {
    x: ['Select', 'Insert', 'Update', 'Delete'],
    y: [
      data.otherNodesDistribution.numSelect,
      data.otherNodesDistribution.numInsert,
      data.otherNodesDistribution.numUpdate,
      data.otherNodesDistribution.numDelete
    ],
    hovertemplate: '%{y}<extra></extra>',
    width: 0.2,
    marker: {
      color: '#262666'
    },
    type: 'bar',
    name: CONST_VAR.AVG_NODES
  };

  useEffect(() => {
    if (!_.isEqual(previousData, data)) {
      const chartData = [maxNodeData, otherNodeData];
      const layout = {
        showlegend: true,
        autosize: true,
        height: 170,
        barmode: 'group',
        bargap: 0.6,
        margin: {
          l: 55,
          b: 30,
          t: 10
        }
      };
      Plotly.newPlot('querySkewLoadGraph', chartData, layout, { displayModeBar: false });
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
              href={EXTERNAL_LINKS.QUERY_LOAD_SKEW}
            >
              {t('clusterDetail.performance.advisor.LearnHow')}
            </a>
          </span>
        </div>
      </div>
      <div className="chartBox">
        <span className="queryText">{t('clusterDetail.performance.chartTitle.Queries')}</span>
        <div id="querySkewLoadGraph"></div>
      </div>
    </div>
  );
};

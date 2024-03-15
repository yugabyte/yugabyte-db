import { useEffect } from 'react';
import { usePrevious } from 'react-use';
import { useTranslation } from 'react-i18next';
import { BrowserRouter as Router, Link } from 'react-router-dom';
import { Box, Typography } from '@material-ui/core';
import _ from 'lodash';
import { CONST_VAR } from '../../utils/constants';
import { TroubleshootHeader } from '../TroubleshootHeader';
import { YBM_MULTI_REGION_INFO } from '../MockData';

const Plotly = require('plotly.js/lib/index-basic.js');

export const NodeIssueChart = () => {
  const { t } = useTranslation();
  const searchParam = new URLSearchParams(window.location.search);
  const urlParamValue = searchParam.get('data');
  const data = JSON.parse(urlParamValue!);

  // if (!data) {
  //   return <Box>{'error'}</Box>;
  // }

  const previousData = usePrevious(data);
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
        height: 270,
        barmode: 'group',
        bargap: 0.6,
        margin: {
          l: 55,
          b: 30,
          t: 10
        }
      };
      Plotly.newPlot('nodeIssueGraph', chartData, layout, { displayModeBar: false });
    }
  }, []);

  return (
    <Box>
      <Router>
        <Link to={{ pathname: `/troubleshoot` }}>
          <Typography variant="h6" className="content-title">
            {'Troubleshoot'}
          </Typography>
        </Link>
      </Router>
      <TroubleshootHeader data={YBM_MULTI_REGION_INFO} />
      <Box m={3} id="nodeIssueGraph"></Box>
    </Box>
  );
};

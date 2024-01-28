import { FC } from 'react';
// import { useTranslation } from "react-i18next";
import { Box } from '@material-ui/core';
import { BrowserRouter as Router, Link } from 'react-router-dom';
import { NodeIssueProps } from '../../utils/dtos';
import { useStyles } from '../DiagnosticStyles';
import LightBulbIcon from '../../assets/lightbulb.svg';

export const NodeIssue: FC<NodeIssueProps> = ({ data, summary, uuid }) => {
  const classes = useStyles();
  // const { t } = useTranslation();

  return (
    <Box>
      <Box className={classes.troubleshootBox}>
        <span> {summary} </span>
        <Box className={classes.recommendationAdvice}>
          <img src={LightBulbIcon} alt="more" className={classes.learnMoreImage} />
          <Router>
            <Link
              to={{ pathname: `/troubleshoot/${uuid}`, state: { data: JSON.stringify(data) } }}
              className={classes.learnRecommendationSuggestions}
            >
              {'Refer to details'}
            </Link>
          </Router>
        </Box>
      </Box>
    </Box>
  );
};

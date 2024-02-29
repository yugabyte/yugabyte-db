import { ChangeEvent, useState, FC, ReactNode, Fragment } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Box } from '@material-ui/core';
import { YBCheckbox } from '@yugabytedb/ui-components';
import {
  IndexAndShardingRecommendationData,
  PerfRecommendationData,
  TroubleshootType
} from '../utils/dtos';
import { assertUnreachableCase } from '../utils/errorHandlingUtils';
import { NodeIssue } from './issues/NodeIssue';
import TraingleDownIcon from '../assets/traingle-down.svg';
import TraingleUpIcon from '../assets/traingle-up.svg';
import { useStyles } from './DiagnosticStyles';

interface TroubleshootDataProps {
  type: TroubleshootType;
  data: PerfRecommendationData | IndexAndShardingRecommendationData;
  key: string;
  uuid: string;
  idKey: string;
  resolved: boolean;
  onResolve: (key: string, value: boolean) => void;
}

const getRecommendation = (type: any, summary: ReactNode | string, data: any, uuid: string) => {
  if (type === TroubleshootType.APP_ISSUE) {
    return null;
  }
  if (type === TroubleshootType.SQL_ISSUE) {
    return null;
  }
  if (type === TroubleshootType.NODE_ISSUE) {
    const highestNodeQueryCount =
      data.recommendationInfo.node_with_highest_query_load_details.SelectStmt +
      data.recommendationInfo.node_with_highest_query_load_details.InsertStmt +
      data.recommendationInfo.node_with_highest_query_load_details.UpdateStmt +
      data.recommendationInfo.node_with_highest_query_load_details.DeleteStmt;

    const otherNodeQueryCount =
      data.recommendationInfo.other_nodes_average_query_load_details.SelectStmt +
      data.recommendationInfo.other_nodes_average_query_load_details.InsertStmt +
      data.recommendationInfo.other_nodes_average_query_load_details.UpdateStmt +
      data.recommendationInfo.other_nodes_average_query_load_details.DeleteStmt;
    const percentDiff = Math.round(
      (100 * (highestNodeQueryCount - otherNodeQueryCount)) / otherNodeQueryCount
    );

    const cpuSkewData = {
      suggestion: data.suggestion,
      maxNodeName: data.target,
      percentDiff,
      maxNodeDistribution: {
        numSelect: data.recommendationInfo.node_with_highest_query_load_details.SelectStmt,
        numInsert: data.recommendationInfo.node_with_highest_query_load_details.InsertStmt,
        numUpdate: data.recommendationInfo.node_with_highest_query_load_details.UpdateStmt,
        numDelete: data.recommendationInfo.node_with_highest_query_load_details.DeleteStmt
      },
      otherNodesDistribution: {
        numSelect: data.recommendationInfo.other_nodes_average_query_load_details.SelectStmt,
        numInsert: data.recommendationInfo.other_nodes_average_query_load_details.InsertStmt,
        numUpdate: data.recommendationInfo.other_nodes_average_query_load_details.UpdateStmt,
        numDelete: data.recommendationInfo.other_nodes_average_query_load_details.DeleteStmt
      }
    };

    return <NodeIssue data={cpuSkewData} summary={summary} uuid={uuid} />;
  }
  if (type === TroubleshootType.INFRA_ISSUE) {
    return null;
  }
  return null;
};

export const DiagnosticBox: FC<TroubleshootDataProps> = ({
  idKey,
  type,
  uuid,
  data,
  resolved,
  onResolve
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [open, setOpen] = useState(false);

  const handleResolveRecommendation = (event: ChangeEvent<HTMLInputElement>) => {
    const isChecked = event.target.checked;
    onResolve(idKey, isChecked);
    if (isChecked) {
      setOpen(false);
    }
    event.stopPropagation();
  };

  const handleOpenBox = () => {
    if (!resolved) {
      setOpen((val) => !val);
    }
  };

  const getTypeTagColor = (recommendationType: TroubleshootType) => {
    switch (recommendationType) {
      case TroubleshootType.APP_ISSUE:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagGreen)}>
            {'APPLICATION ISSUE'}
          </span>
        );
      case TroubleshootType.INFRA_ISSUE:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagGreen)}>
            {'INFRASTRUCTURE ISSUE'}
          </span>
        );
      case TroubleshootType.NODE_ISSUE:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagBlue)}>{'NODE ISSUE'}</span>
        );
      case TroubleshootType.SQL_ISSUE:
        return (
          <span className={clsx(classes.troubleshootTitle, classes.tagBlue)}>{'SQL ISSUE'}</span>
        );
      case TroubleshootType.ALL:
        return null;
      default:
        return assertUnreachableCase(recommendationType);
    }
  };

  const getSummaryContent = (recommendationType: TroubleshootType, recommendationData: any) => {
    if (recommendationType === TroubleshootType.APP_ISSUE) {
      return (
        <Fragment>
          <span className={classes.troubleshootHeaderDescription}>{}</span>
        </Fragment>
      );
    } else if (recommendationType === TroubleshootType.INFRA_ISSUE) {
      return (
        <Fragment>
          <span className={classes.troubleshootHeaderDescription}>{}</span>
        </Fragment>
      );
    } else {
      const { observation } = recommendationData;
      return (
        <Fragment>
          <span className={classes.troubleshootHeaderDescription}>{observation}</span>
        </Fragment>
      );
    }
  };

  return (
    <Box className={classes.recommendation}>
      <Box
        onClick={handleOpenBox}
        display="flex"
        alignItems="center"
        className={resolved ? classes.strikeThroughText : classes.itemHeader}
      >
        {getTypeTagColor(type)}
        <span>{!open && getSummaryContent(type, data)}</span>
        <Box ml="auto">
          <YBCheckbox
            label={'Resolved'}
            onChange={handleResolveRecommendation}
            checked={resolved}
          />
        </Box>
        {open ? (
          <img src={TraingleDownIcon} alt="expand" />
        ) : (
          <img
            src={TraingleUpIcon}
            alt="shrink"
            className={clsx(resolved && classes.inactiveIssue)}
          />
        )}
      </Box>
      {open && getRecommendation(type, getSummaryContent(type, data), data, uuid)}
    </Box>
  );
};

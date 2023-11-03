import { ChangeEvent, useState, FC, ReactNode, Fragment } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Box } from '@material-ui/core';
import { YBCheckbox } from '../../redesign/components';
import { IndexSuggestion } from './advisor/IndexSuggestion';
import { SchemaSuggestion } from './advisor/SchemaSuggestion';
import { QueryLoadSkew } from './advisor/QueryLoadSkew';
import { ConnectionSkew } from './advisor/ConnectionSkew';
import { CpuSkew } from './advisor/CpuSkew';
import { HotShard } from './advisor/HotShard';
import { CustomRecommendations } from './advisor/CustomRecommendation';
import {
  RecommendationType,
  IndexAndShardingRecommendationData,
  PerfRecommendationData
} from '../../redesign/utils/dtos';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';
import TraingleDownIcon from './images/traingle-down.svg';
import TraingleUpIcon from './images/traingle-up.svg';
import { useStyles } from './RecommendationStyles';

interface RecommendationProps {
  type: RecommendationType;
  data: PerfRecommendationData | IndexAndShardingRecommendationData;
  key: string;
  idKey: string;
  resolved: boolean;
  onResolve: (key: string, value: boolean) => void;
}

const getRecommendation = (type: RecommendationType, summary: ReactNode | string, data: any) => {
  if (type === RecommendationType.UNUSED_INDEX) {
    const indexSuggestionData = data.table?.data ?? [];
    return <IndexSuggestion summary={summary} data={indexSuggestionData} />;
  }
  if (type === RecommendationType.RANGE_SHARDING) {
    const schemaSuggestionData = data.table?.data ?? [];
    return <SchemaSuggestion data={schemaSuggestionData} summary={summary} />;
  }
  if (type === RecommendationType.QUERY_LOAD_SKEW) {
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

    return <QueryLoadSkew data={cpuSkewData} summary={summary} />;
  }
  if (type === RecommendationType.CONNECTION_SKEW) {
    const maxConnectionsNodeName = data.recommendationInfo.node_with_highest_connection_count;
    const recommendationInfoDetails = data.recommendationInfo.details;
    const maxNodeValue = recommendationInfoDetails?.[maxConnectionsNodeName];
    const connectionSkewData = {
      suggestion: data.suggestion,
      maxNodeName: maxConnectionsNodeName,
      maxNodeValue: maxNodeValue ?? 0,
      otherNodesAvgValue: data.recommendationInfo.avg_connection_count_of_other_nodes
    };
    return <ConnectionSkew data={connectionSkewData} summary={summary} />;
  }
  if (type === RecommendationType.CPU_SKEW) {
    const cpuSkewData = {
      suggestion: data.suggestion,
      maxNodeName: data.recommendationInfo.highestNodeName,
      maxNodeValue: data.recommendationInfo.highestNodeCpu ?? 0,
      otherNodesAvgValue: data.recommendationInfo.otherNodesAvgCpu ?? 0
    };
    return <CpuSkew data={cpuSkewData} summary={summary} />;
  }
  if (type === RecommendationType.HOT_SHARD) {
    const hotShardData = {
      suggestion: data.suggestion,
      maxNodeName: data.recommendationInfo.node_with_hot_shard,
      maxNodeValue: data.recommendationInfo.hot_shard_node_operation_count ?? 0,
      otherNodesAvgValue: data.recommendationInfo.avg_query_count_of_other_nodes ?? 0
    };
    return <HotShard data={hotShardData} summary={summary} />;
  }
  if (type === RecommendationType.CPU_USAGE || type === RecommendationType.REJECTED_CONNECTIONS) {
    return <CustomRecommendations summary={summary} suggestion={data.suggestion} type={type} />;
  }
  return null;
};

export const RecommendationBox: FC<RecommendationProps> = ({
  idKey,
  type,
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

  const getTypeTagColor = (recommendationType: RecommendationType) => {
    switch (recommendationType) {
      case RecommendationType.UNUSED_INDEX:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagGreen)}>
            {t('clusterDetail.performance.typeTag.IndexSuggestion')}
          </span>
        );
      case RecommendationType.RANGE_SHARDING:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagGreen)}>
            {t('clusterDetail.performance.typeTag.SchemaSuggestion')}
          </span>
        );
      case RecommendationType.QUERY_LOAD_SKEW:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagBlue)}>
            {t('clusterDetail.performance.typeTag.QueryLoadSkew')}
          </span>
        );
      case RecommendationType.CONNECTION_SKEW:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagBlue)}>
            {t('clusterDetail.performance.typeTag.ConnectionSkew')}
          </span>
        );
      case RecommendationType.CPU_SKEW:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagBlue)}>
            {t('clusterDetail.performance.typeTag.CpuSkew')}
          </span>
        );
      case RecommendationType.CPU_USAGE:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagBlue)}>
            {t('clusterDetail.performance.typeTag.CpuUsage')}
          </span>
        );
      case RecommendationType.HOT_SHARD:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagBlue)}>
            {t('clusterDetail.performance.typeTag.HotShard')}
          </span>
        );
      case RecommendationType.REJECTED_CONNECTIONS:
        return (
          <span className={clsx(classes.recommendationTitle, classes.tagBlue)}>
            {t('clusterDetail.performance.typeTag.RejectedConnections')}
          </span>
        );
      case RecommendationType.ALL:
        return null;
      default:
        return assertUnreachableCase(recommendationType);
    }
  };

  const getSummaryContent = (recommendationType: RecommendationType, recommendationData: any) => {
    if (recommendationType === RecommendationType.UNUSED_INDEX) {
      const { indicator: numIndexes, target: databaseName } = recommendationData;
      return (
        <Fragment>
          <span className={classes.recommendationHeaderDescription}>
            {`We found ${numIndexes} indexes in database `}
            <b>{databaseName}</b>
            {' that were never used.'}
          </span>
        </Fragment>
      );
    } else if (recommendationType === RecommendationType.RANGE_SHARDING) {
      const { indicator: numIndexes, target: databaseName } = recommendationData;
      return (
        <Fragment>
          <span className={classes.recommendationHeaderDescription}>
            {`We found ${numIndexes} indexes in database `}
            <b> {databaseName} </b>
            {' where using range sharding instead of hash can improve query speed.'}
          </span>
        </Fragment>
      );
    } else {
      const { observation } = recommendationData;
      return (
        <Fragment>
          <span className={classes.recommendationHeaderDescription}>{observation}</span>
        </Fragment>
      );
    }
  };

  return (
    <div className={classes.recommendation}>
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
            className={clsx(resolved && classes.inactiveRecommendation)}
          />
        )}
      </Box>
      {open && getRecommendation(type, getSummaryContent(type, data), data)}
    </div>
  );
};

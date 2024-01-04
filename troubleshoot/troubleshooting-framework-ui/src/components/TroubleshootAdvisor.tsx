import { useState } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import { DiagnosticBox } from './DiagnosticBox';

const MOCK_DATA = [
  {
    data: {
      entityType: 'NODE',
      isStale: false,
      new: false,
      observation:
        'Node yb-admin-test-master-placement-n2 processed 39410.69% more queries than average of other 2 nodes',
      recommendationPriority: 'MEDIUM',
      recommendationState: 'OPEN',
      recommendationInfo: {
        node_with_highest_query_load_details: {
          DeleteStmt: 24,
          InsertStmt: 15685,
          SelectStmt: 15702,
          UpdateStmt: 0
        },
        other_nodes_average_query_load_details: {
          DeleteStmt: 24,
          InsertStmt: 24,
          SelectStmt: 31.5,
          UpdateStmt: 0
        }
      },
      recommendationTimestamp: 1702047631.751105,
      suggestion: 'Redistribute queries to other nodes in the cluster.',
      target: 'yb-admin-test-master-placement-n2',
      type: 'NODE_ISSUE'
    },
    key: 'NODE_ISSUE-yb-admin-test-master-placement-n2-08cbf046a0505',
    uuid: '6e2f-779e'
  },
  {
    data: {
      entityType: 'NODE',
      isStale: false,
      new: false,
      observation:
        'Node yb-admin-test-master-placement-n1 processed 3008.71% more queries than average of other 2 nodes',
      recommendationPriority: 'MEDIUM',
      recommendationState: 'OPEN',
      recommendationInfo: {
        node_with_highest_query_load_details: {
          DeleteStmt: 11000,
          InsertStmt: 9020,
          SelectStmt: 37,
          UpdateStmt: 0
        },
        other_nodes_average_query_load_details: {
          DeleteStmt: 150,
          InsertStmt: 21,
          SelectStmt: 31.5,
          UpdateStmt: 0
        }
      },
      recommendationTimestamp: 1702047631.751105,
      suggestion: 'Redistribute queries to other nodes in the cluster.',
      target: 'yb-admin-test-master-placement-n1',
      type: 'NODE_ISSUE'
    },
    key: 'NODE_ISSUE-yb-admin-test-master-placement-n1-08cbf046a0505',
    uuid: '6e9l-30dw'
  },
  {
    data: {
      entityType: 'NODE',
      isStale: false,
      new: false,
      observation:
        'Node yb-admin-test-master-placement-n3 processed 1055.26% more queries than average of other 2 nodes',
      recommendationPriority: 'MEDIUM',
      recommendationState: 'OPEN',
      recommendationInfo: {
        node_with_highest_query_load_details: {
          DeleteStmt: 24,
          InsertStmt: 6300,
          SelectStmt: 4075,
          UpdateStmt: 0
        },
        other_nodes_average_query_load_details: {
          DeleteStmt: 24,
          InsertStmt: 24,
          SelectStmt: 31.5,
          UpdateStmt: 0
        }
      },
      recommendationTimestamp: 1702047631.751105,
      suggestion: 'Redistribute queries to other nodes in the cluster.',
      target: 'yb-admin-test-master-placement-n3',
      type: 'NODE_ISSUE'
    },
    key: 'NODE_ISSUE-yb-admin-test-master-placement-n3-08cbf046a0505',
    uuid: '11k3-56qq'
  }
];

interface TroubleshootAdvisorProps {
  universeUUID?: string;
  clusterUUID?: string;
  data: any;
}

const useStyles = makeStyles((theme) => ({
  troubleshootAdvisorBox: {
    marginRight: theme.spacing(1)
  }
}));

interface TroubleshootDetailProps {
  data: any;
  key: string;
  isResolved?: boolean;
}

export const TroubleshootAdvisor = ({
  data,
  universeUUID,
  clusterUUID
}: TroubleshootAdvisorProps) => {
  const classes = useStyles();
  const [displayedRecomendations, setDisplayedRecommendations] = useState<
    TroubleshootDetailProps[]
  >(MOCK_DATA);

  const handleResolve = (id: string, isResolved: boolean) => {
    const copyRecommendations: any = [...MOCK_DATA];
    const userSelectedRecommendation = copyRecommendations.find((rec: any) => rec.key === id);
    if (userSelectedRecommendation) {
      userSelectedRecommendation.isResolved = isResolved;
    }
    setDisplayedRecommendations(copyRecommendations);
  };

  return (
    // This dialog is shown is when the last run API fails with 404
    <Box className={classes.troubleshootAdvisorBox}>
      {displayedRecomendations.map((rec: any) => (
        <>
          <DiagnosticBox
            key={rec.key}
            idKey={rec.key}
            uuid={rec.uuid}
            type={rec.data.type}
            data={rec.data}
            resolved={!!rec.isResolved}
            onResolve={handleResolve}
          />
        </>
      ))}
    </Box>
  );
};

import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';
import { useDispatch, useSelector } from 'react-redux';
import { useQuery, useQueryClient } from 'react-query';

import {
  ConfigureInstanceTypeFormValues,
  ConfigureInstanceTypeModal
} from './ConfigureInstanceTypeModal';
import OnPremNodesListContainer from '../../../../config/OnPrem/OnPremNodesListContainer';
import { DeleteInstanceTypeModal } from './DeleteInstanceTypeModal';
import { InstanceTypeList } from './InstanceTypeList';
import { InstanceTypeOperation, ProviderCode } from '../../constants';
import { YBButton } from '../../../../../redesign/components';
import { YBErrorIndicator, YBLoading } from '../../../../common/indicators';
import { api, instanceTypeQueryKey } from '../../../../../redesign/helpers/api';
import { openDialog } from '../../../../../actions/modal';
import {
  getInstanceTypeList,
  getInstanceTypeListResponse,
  getNodeInstancesForProvider,
  getNodesInstancesForProviderResponse
} from '../../../../../actions/cloud';
import {
  useUpdateInstanceType,
  useDeleteInstanceType
} from '../../../../../redesign/helpers/hooks';

import { InstanceType } from '../../../../../redesign/helpers/dtos';
import { OnPremProvider } from '../../types';
import { NodeAgentStatusModal } from './NodeAgentStatusModal';
import { SortDirection } from '../../../../../redesign/utils/dtos';
import { NodeAgentAPI, QUERY_KEY } from '../../../../../redesign/features/NodeAgent/api';
import { isNonEmptyArray } from '../../../../../utils/ObjectUtils';
import { formatNumberToText } from '../../../../../utils/Formatters';
import ErrorIcon from '../../../../../redesign/assets/error.svg';

interface ManageInstancesProps {
  providerConfig: OnPremProvider;
}

const useStyles = makeStyles((theme) => ({
  actionBar: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-end',
    gap: theme.spacing(1.5),

    paddingBottom: theme.spacing(3)
  },
  subHeading: {
    padding: theme.spacing(3, 0)
  },
  nodeAgentStatusContainer: {
    height: theme.spacing(6),
    background: 'rgba(231, 62, 54, 0.15)',
    border: '1px solid rgba(231, 62, 54, 0.25)',
    borderRadius: theme.spacing(1),
    padding: '8px 16px',
    marginBottom: theme.spacing(2)
  },
  nodeAgentStatusText: {
    fontSize: '13px',
    fontFamily: 'Inter',
    fontWeight: 600,
    marginLeft: theme.spacing(0.5),
    marginRight: theme.spacing(1),
    color: '#0B1117'
  },
  nodeAgentErrorImage: {
    marginBottom: theme.spacing(0.25)
  }
}));

export const ManageInstances = ({ providerConfig }: ManageInstancesProps) => {
  const [isInstanceTypeFormModalOpen, setIsInstanceTypeFormModalOpen] = useState<boolean>(false);
  const [isDeleteInstanceTypeModalOpen, setIsDeleteInstanceTypeModalOpen] = useState<boolean>(
    false
  );
  const [isNodeAgentStatusModalOpen, setIsNodeAgentStatusModalOpen] = useState<boolean>(false);
  const [instanceTypeSelection, setInstanceTypeSelection] = useState<InstanceType>();
  const [isNodeAgentHealthDown, setIsNodeAgentHealthDown] = useState<boolean>(false);
  const [numNodesHealthDown, setNumNodesHealthDown] = useState<number>(0);
  const nodeInstanceList = useSelector((state: any) => state.cloud.nodeInstanceList);

  const { t } = useTranslation();
  const classes = useStyles();
  const dispatch = useDispatch();
  const providerUUID = providerConfig.uuid;

  const nodeIPs = nodeInstanceList?.data?.map((nodeInstance: any) => {
    return nodeInstance.details.ip;
  });
  const payload: any = {
    filter: {
      nodeIps: nodeIPs
    },
    sortBy: 'ip',
    direction: SortDirection.DESC,
    offset: 0,
    limit: 500,
    needTotalCount: true
  };
  const nodeAgentStatusByIPs = useQuery(
    QUERY_KEY.fetchNodeAgentByIPs,
    () => NodeAgentAPI.fetchNodeAgentByIPs(payload),
    {
      enabled: isNonEmptyArray(nodeIPs),
      onSuccess: (data: any) => {
        const nodeAgentOnPremNodes = data.entities;
        const numNodeAgentsFailed =
          nodeAgentOnPremNodes?.filter(
            (nodeAgentOnPremNode: any) => !nodeAgentOnPremNode?.reachable
          ) ?? [];
        setNumNodesHealthDown(numNodeAgentsFailed.length);
        setIsNodeAgentHealthDown(isNonEmptyArray(numNodeAgentsFailed));
      }
    }
  );

  useEffect(() => {
    dispatch(getNodeInstancesForProvider(providerUUID) as any).then((response: any) => {
      dispatch(getNodesInstancesForProviderResponse(response.payload));
    });
    dispatch(getInstanceTypeList(providerUUID) as any).then((response: any) => {
      dispatch(getInstanceTypeListResponse(response.payload));
    });
  }, [providerUUID, dispatch]);

  const queryClient = useQueryClient();
  const updateCachedInstanceTypes = (providerUUID: string) => {
    queryClient.invalidateQueries(instanceTypeQueryKey.ALL, { exact: true });
    queryClient.invalidateQueries(instanceTypeQueryKey.provider(providerUUID));

    // `OnPremNodesListContainer` uses the instance types stored in the redux store rather than what
    //  is cached by react-query. Thus, we need to also update the store at this point in time.
    dispatch(getInstanceTypeList(providerUUID) as any).then((response: any) => {
      dispatch(getInstanceTypeListResponse(response.payload));
    });
  };
  const createInstanceTypeMutation = useUpdateInstanceType(queryClient, {
    onSuccess: (_response, variables) => updateCachedInstanceTypes(variables.providerUUID)
  });
  const deleteInstanceTypeMutation = useDeleteInstanceType(queryClient, {
    onSuccess: (_response, variables) => updateCachedInstanceTypes(variables.providerUUID)
  });
  const instanceTypeQuery = useQuery(instanceTypeQueryKey.provider(providerUUID), () =>
    api.fetchInstanceTypes(providerUUID)
  );

  if (instanceTypeQuery.isLoading || instanceTypeQuery.isIdle) {
    return <YBLoading />;
  }
  if (instanceTypeQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={`Error fetching instance types for provider: ${providerUUID}`}
      />
    );
  }
  const instanceTypes = instanceTypeQuery.data;

  const onInstanceTypeFormSubmit = (instanceTypeValues: ConfigureInstanceTypeFormValues) => {
    const DEFAULT_VOLUME_TYPE = 'SSD';

    const instanceType = {
      idKey: {
        instanceTypeCode: instanceTypeValues.instanceTypeCode,
        providerCode: ProviderCode.ON_PREM
      },
      numCores: instanceTypeValues.numCores,
      memSizeGB: instanceTypeValues.memSizeGB,
      instanceTypeDetails: {
        volumeDetailsList: instanceTypeValues.mountPaths.split(',').map((mountPath) => ({
          volumeSizeGB: instanceTypeValues.volumeSizeGB,
          mountPath: mountPath.trim(),
          volumeType: DEFAULT_VOLUME_TYPE
        }))
      }
    };
    return createInstanceTypeMutation.mutate({
      providerUUID: providerUUID,
      instanceType: instanceType
    });
  };

  const onDeleteInstanceTypeSubmit = (currentInstanceType: InstanceType) => {
    deleteInstanceTypeMutation.mutate({
      providerUUID: providerUUID,
      instanceTypeCode: currentInstanceType.instanceTypeCode
    });
  };

  const showNodeAgentStatusModal = () => {
    setIsNodeAgentStatusModalOpen(true);
  };
  const hideNodeAgentStatusModal = () => {
    setIsNodeAgentStatusModalOpen(false);
  };
  const showAddInstanceTypeFormModal = () => {
    setInstanceTypeSelection(undefined);
    setIsInstanceTypeFormModalOpen(true);
  };
  const hideInstanceFormModal = () => {
    setIsInstanceTypeFormModalOpen(false);
  };
  const showDeleteInstanceTypeModal = (selectedInstanceType: InstanceType) => {
    setInstanceTypeSelection(selectedInstanceType);
    setIsDeleteInstanceTypeModalOpen(true);
  };
  const hideDeleteInstanceTypeModal = () => {
    setIsDeleteInstanceTypeModalOpen(false);
  };
  const showInstancesFormModal = () => {
    dispatch(openDialog('AddNodesForm'));
  };

  return (
    <div>
      {isNodeAgentHealthDown && (
        <div className={classes.nodeAgentStatusContainer}>
          <img className={classes.nodeAgentErrorImage} src={ErrorIcon} alt="error" />
          <span className={classes.nodeAgentStatusText}>
            {`${formatNumberToText(numNodesHealthDown)} `}
            {t('nodeAgent.unreachableInstanceAgents')}
          </span>
          <YBButton variant="secondary" type="button" onClick={showNodeAgentStatusModal}>
            {t('nodeAgent.viewDetailsButton')}
          </YBButton>
        </div>
      )}
      <div className={classes.actionBar}>
        <YBButton
          style={{ justifySelf: 'flex-end', width: '200px' }}
          variant="primary"
          type="button"
          onClick={showAddInstanceTypeFormModal}
        >
          <i className="fa fa-plus" />
          Add Instance Type
        </YBButton>
        <YBButton
          style={{ justifySelf: 'flex-end', width: '200px' }}
          variant="primary"
          type="button"
          onClick={showInstancesFormModal}
        >
          <i className="fa fa-plus" />
          Add Instances
        </YBButton>
      </div>
      <Typography variant="h4" classes={{ h4: classes.subHeading }}>
        Instance Types
      </Typography>
      <InstanceTypeList
        instanceTypes={instanceTypes}
        showAddInstanceTypeFormModal={showAddInstanceTypeFormModal}
        showDeleteInstanceTypeModal={showDeleteInstanceTypeModal}
      />
      <Typography variant="h4" classes={{ h4: classes.subHeading }}>
        Instances
      </Typography>
      <OnPremNodesListContainer
        selectedProviderUUID={providerUUID}
        isRedesignedView={true}
        currentProvider={providerConfig}
        nodeAgentStatusByIPs={nodeAgentStatusByIPs}
        isNodeAgentHealthDown={isNodeAgentHealthDown}
      />
      {isInstanceTypeFormModalOpen && (
        <ConfigureInstanceTypeModal
          instanceTypeOperation={InstanceTypeOperation.ADD}
          onClose={hideInstanceFormModal}
          onInstanceTypeSubmit={onInstanceTypeFormSubmit}
          open={isInstanceTypeFormModalOpen}
        />
      )}
      {isDeleteInstanceTypeModalOpen && (
        <DeleteInstanceTypeModal
          instanceType={instanceTypeSelection}
          onClose={hideDeleteInstanceTypeModal}
          open={isDeleteInstanceTypeModalOpen}
          deleteInstanceType={onDeleteInstanceTypeSubmit}
        />
      )}
      {isNodeAgentStatusModalOpen && (
        <NodeAgentStatusModal
          nodeIPs={nodeIPs}
          onClose={hideNodeAgentStatusModal}
          open={isNodeAgentStatusModalOpen}
          isAssignedNodes={false}
        />
      )}
    </div>
  );
};

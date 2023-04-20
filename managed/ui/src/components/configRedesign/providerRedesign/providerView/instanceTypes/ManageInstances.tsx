import React, { useEffect, useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useDispatch } from 'react-redux';
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
  }
}));

export const ManageInstances = ({ providerConfig }: ManageInstancesProps) => {
  const [isInstanceTypeFormModalOpen, setIsInstanceTypeFormModalOpen] = useState<boolean>(false);
  const [isDeleteInstanceTypeModalOpen, setIsDeleteInstanceTypeModalOpen] = useState<boolean>(
    false
  );
  const [instanceTypeSelection, setInstanceTypeSelection] = useState<InstanceType>();
  const classes = useStyles();
  const dispatch = useDispatch();
  const providerUUID = providerConfig.uuid;
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
    </div>
  );
};

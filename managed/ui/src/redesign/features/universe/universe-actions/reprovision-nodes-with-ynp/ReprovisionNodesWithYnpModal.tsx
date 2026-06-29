import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';
import Select, { ValueType } from 'react-select';

import {
  OptionProps,
  RadioGroupOrientation,
  YBModal,
  YBModalProps,
  YBRadioGroup
} from '../../../../components';
import { fetchTaskUntilItCompletes } from '../../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../../utils/errorHandlingUtils';
import { api, universeQueryKey } from '../../../../helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../../../components/common/indicators';

import toastStyles from '../../../../../redesign/styles/toastStyles.module.scss';

interface ReprovisionNodesWithYnpModalProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

type NodeNameOption = {
  label: string;
  value: string;
};

const useStyles = makeStyles((theme) => ({
  radioButtonGroup: {
    gap: theme.spacing(2)
  }
}));

const NodeSelectionOption = {
  UNIVERSE: 'universe',
  NODE: 'node'
} as const;
type NodeSelectionOption = (typeof NodeSelectionOption)[keyof typeof NodeSelectionOption];

const MODAL_NAME = 'ReprovisionNodesWithYnpModal';
const TRANSLATION_KEY_PREFIX = 'universeActions.reprovisionNodesWithYnpModal';

export const ReprovisionNodesWithYnpModal = ({
  universeUuid,
  modalProps
}: ReprovisionNodesWithYnpModalProps) => {
  const [nodeSelectionOption, setNodeSelectionOption] = useState<NodeSelectionOption>(
    NodeSelectionOption.UNIVERSE
  );
  const [selectedNodeNameOption, setSelectedNodeNameOption] = useState<NodeNameOption>();
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const universeQuery = useQuery(universeQueryKey.detail(universeUuid), () =>
    api.fetchUniverse(universeUuid)
  );
  const universeNodeNames =
    universeQuery.data?.universeDetails.nodeDetailsSet
      .filter((nodeDetails) => !!nodeDetails.nodeName)
      .map((nodeDetails) => nodeDetails.nodeName as string)
      .sort((nodeNameA, nodeNameB) => nodeNameA?.localeCompare(nodeNameB)) ?? [];

  const firstNodeName = universeNodeNames[0];
  useEffect(() => {
    if (firstNodeName && !selectedNodeNameOption) {
      setSelectedNodeNameOption({ label: firstNodeName, value: firstNodeName });
    }
  }, [firstNodeName]);

  const getNodeNamesForReprovision = (): string[] => {
    if (nodeSelectionOption === NodeSelectionOption.UNIVERSE) {
      // The server will interpret an empty array as "all nodes in the universe".
      return [];
    }
    return [selectedNodeNameOption?.value ?? ''];
  };

  const reprovisionNodesWithYnpMutation = useMutation(
    () =>
      api.provisionUniverseNodes(universeUuid, {
        nodeNames: getNodeNamesForReprovision()
      }),
    {
      onSuccess: (response) => {
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <Typography variant="body2" className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                {t('error.taskFailure')}
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </Typography>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t('success.taskSuccess')}
              </Typography>
            );
          }
        };

        modalProps.onClose();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  const resetModal = () => {
    setIsSubmitting(false);
  };
  const onSubmit = () => {
    setIsSubmitting(true);
    reprovisionNodesWithYnpMutation.mutate(undefined /** variables */, {
      onSettled: () => resetModal()
    });
  };

  const handleNodeSelectionOptionChange = (event: React.ChangeEvent<HTMLInputElement>): void => {
    const value = event.target.value as NodeSelectionOption;
    setNodeSelectionOption(value);
  };
  const handleSelectedNodeChange = (value: ValueType<NodeNameOption>) => {
    if (value && !Array.isArray(value)) {
      setSelectedNodeNameOption(value as NodeNameOption);
    }
  };

  const NODE_SELECTION_OPTIONS: OptionProps[] = [
    { value: NodeSelectionOption.UNIVERSE, label: t('allNodeInUniverse') },
    { value: NodeSelectionOption.NODE, label: t('selectedNode') }
  ];
  const universeNodeNameOptions = universeNodeNames.map((nodeName) => ({
    label: nodeName,
    value: nodeName
  }));

  const isNodeNameFieldDisabled = nodeSelectionOption === NodeSelectionOption.UNIVERSE;
  const isSubmitDisabled =
    nodeSelectionOption === NodeSelectionOption.NODE && !selectedNodeNameOption?.value;

  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (universeQuery.isLoading) {
    return (
      <YBModal
        title={t('title')}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        buttonProps={{ primary: { disabled: true } }}
        size="md"
        {...modalProps}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (universeQuery.isError) {
    return (
      <YBModal
        title={t('title')}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        buttonProps={{ primary: { disabled: true } }}
        size="md"
        {...modalProps}
      >
        <YBErrorIndicator
          customErrorMessage={t('failedToFetchSpecificUniverse', {
            keyPrefix: 'queryError',
            universeUuid
          })}
        />
      </YBModal>
    );
  }

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('confirmButton')}
      cancelLabel={cancelLabel}
      onSubmit={onSubmit}
      isSubmitting={isSubmitting}
      buttonProps={{ primary: { disabled: isSubmitDisabled } }}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      size="md"
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} height="100%">
        <Typography variant="body2">{t('instructions')}</Typography>
        <Typography variant="body2">{t('reprovisionNodesFor')}</Typography>
        <YBRadioGroup
          className={classes.radioButtonGroup}
          orientation={RadioGroupOrientation.VERTICAL}
          options={NODE_SELECTION_OPTIONS}
          value={nodeSelectionOption}
          onChange={handleNodeSelectionOptionChange}
        />
        <Box width={340} maxWidth="100%" marginLeft={3}>
          <Select
            value={selectedNodeNameOption}
            onChange={handleSelectedNodeChange}
            options={universeNodeNameOptions}
            isDisabled={isNodeNameFieldDisabled}
            isClearable={false}
          />
        </Box>
      </Box>
    </YBModal>
  );
};

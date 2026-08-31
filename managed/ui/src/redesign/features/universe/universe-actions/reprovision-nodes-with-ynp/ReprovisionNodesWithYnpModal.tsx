import { useState } from 'react';
import { Box, Link as MUILink, makeStyles, Typography, useTheme } from '@material-ui/core';
import { RadioOrientation, YBAutoComplete, YBRadioGroup } from '@yugabyte-ui-library/core';
import { AxiosError } from 'axios';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';

import { YBModal, YBModalProps } from '../../../../components';
import { fetchTaskUntilItCompletes } from '../../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../../utils/errorHandlingUtils';
import { getPrimaryCluster } from '../../../../../utils/universeUtilsTyped';
import { api, universeQueryKey } from '../../../../helpers/api';
import { Universe } from '../../../../helpers/dtos';
import { YBErrorIndicator, YBLoading } from '../../../../../components/common/indicators';

import toastStyles from '../../../../../redesign/styles/toastStyles.module.scss';

import DocumentationIcon from '@app/redesign/assets/documentation.svg';
import InfoIcon from '@app/redesign/assets/info-blue.svg';
import InfoErrorIcon from '@app/redesign/assets/info-red.svg';

const REPROVISION_NODES_DOCUMENTATION_URL =
  'https://docs.yugabyte.com/stable/yugabyte-platform/prepare/server-nodes-software/';

interface ReprovisionNodesWithYnpModalProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

interface ReprovisionNodesWithYnpFormProps {
  universe: Universe;
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
  },
  nodeNameFieldContainer: {
    width: 340,
    maxWidth: '100%',
    marginLeft: theme.spacing(4)
  },
  footerNotes: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    width: '100%',
    marginTop: 'auto'
  },
  learnMoreLink: {
    display: 'inline-flex',
    alignItems: 'center',
    alignSelf: 'flex-start',
    gap: theme.spacing(0.5),

    color: theme.palette.primary[600],
    cursor: 'pointer',
    lineHeight: '16px',

    '&:hover': {
      color: theme.palette.primary[600]
    }
  },
  rollingRestartBanner: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: theme.spacing(1),

    width: '100%',
    padding: theme.spacing(1),

    backgroundColor: theme.palette.info[100],
    borderRadius: theme.shape.borderRadius
  },
  rollingRestartBannerError: {
    backgroundColor: theme.palette.error[100]
  },
  rollingRestartBannerIcon: {
    flexShrink: 0,

    width: 24,
    height: 24
  },
  rollingRestartBannerText: {
    flex: 1,

    paddingTop: theme.spacing(0.5),
    paddingBottom: theme.spacing(0.5),

    color: theme.palette.grey[900],
    fontSize: 13,
    lineHeight: '16px'
  }
}));

const HA_REPLICATION_FACTOR_THRESHOLD = 3;

const NodeSelectionOption = {
  UNIVERSE: 'universe',
  NODE: 'node'
} as const;
type NodeSelectionOption = (typeof NodeSelectionOption)[keyof typeof NodeSelectionOption];

const MODAL_NAME = 'ReprovisionNodesWithYnpModal';
const TRANSLATION_KEY_PREFIX = 'universeActions.reprovisionNodesWithYnpModal';

const getUniverseNodeNames = (universe: Universe): string[] =>
  universe.universeDetails.nodeDetailsSet
    .filter((nodeDetails) => !!nodeDetails.nodeName)
    .map((nodeDetails) => nodeDetails.nodeName as string)
    .sort((nodeNameA, nodeNameB) => nodeNameA?.localeCompare(nodeNameB));

export const ReprovisionNodesWithYnpModal = ({
  universeUuid,
  modalProps
}: ReprovisionNodesWithYnpModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const cancelLabel = t('cancel', { keyPrefix: 'common' });

  const universeQuery = useQuery(universeQueryKey.detail(universeUuid), () =>
    api.fetchUniverse(universeUuid)
  );

  if (universeQuery.isLoading || universeQuery.isIdle) {
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

  if (universeQuery.isError || !universeQuery.data) {
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
    <ReprovisionNodesWithYnpForm
      universe={universeQuery.data}
      universeUuid={universeUuid}
      modalProps={modalProps}
    />
  );
};

const ReprovisionNodesWithYnpForm = ({
  universe,
  universeUuid,
  modalProps
}: ReprovisionNodesWithYnpFormProps) => {
  const universeNodeNames = getUniverseNodeNames(universe);
  const firstNodeName = universeNodeNames[0];
  const [nodeSelectionOption, setNodeSelectionOption] = useState<NodeSelectionOption>(
    NodeSelectionOption.UNIVERSE
  );
  const [selectedNodeNameOption, setSelectedNodeNameOption] = useState<NodeNameOption | undefined>(
    firstNodeName ? { label: firstNodeName, value: firstNodeName } : undefined
  );
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const universeName = universe.name;
  const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters ?? []);
  const primaryReplicationFactor = primaryCluster?.userIntent?.replicationFactor ?? 0;
  const canRemainOnlineDuringRestart = primaryReplicationFactor >= HA_REPLICATION_FACTOR_THRESHOLD;
  const isReprovisioningSelectedNode = nodeSelectionOption === NodeSelectionOption.NODE;
  const getRestartNoteI18nKey = () => {
    if (canRemainOnlineDuringRestart) {
      return isReprovisioningSelectedNode
        ? 'rollingRestartNoteRF3SelectedNode'
        : 'rollingRestartNoteRF3';
    }
    return isReprovisioningSelectedNode
      ? 'rollingRestartNoteLowRFSelectedNode'
      : 'rollingRestartNoteLowRF';
  };

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
  const handleSelectedNodeChange = (
    _event: React.ChangeEvent<{}>,
    option: string | Record<string, string> | (string | Record<string, string>)[] | null
  ) => {
    if (option && !Array.isArray(option) && typeof option !== 'string') {
      setSelectedNodeNameOption(option as NodeNameOption);
    }
  };

  const isSingleNodeUniverse = universeNodeNames.length <= 1;
  const NODE_SELECTION_OPTIONS = [
    { value: NodeSelectionOption.UNIVERSE, label: t('allNodeInUniverse') },
    {
      value: NodeSelectionOption.NODE,
      label: t('selectedNode'),
      disabled: isSingleNodeUniverse,
      tooltip: isSingleNodeUniverse ? t('selectedNodeDisabledSingleNodeUniverse') : undefined
    }
  ];
  const universeNodeNameOptions = universeNodeNames.map((nodeName) => ({
    label: nodeName,
    value: nodeName
  }));

  const getNodeNameOptionLabel = (option: string | Record<string, string>): string =>
    typeof option === 'string' ? option : option.label;

  const isNodeNameFieldDisabled =
    nodeSelectionOption === NodeSelectionOption.UNIVERSE || isSingleNodeUniverse;
  const isSubmitDisabled =
    nodeSelectionOption === NodeSelectionOption.NODE && !selectedNodeNameOption?.value;
  const cancelLabel = t('cancel', { keyPrefix: 'common' });

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
      dialogContentProps={{ dividers: true, className: classes.modalContent }}
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" height="100%">
        <Typography variant="body2">{t('description')}</Typography>
        <Box
          display="flex"
          flexDirection="column"
          gridGap={theme.spacing(2)}
          flex={1}
          minHeight={0}
          marginTop={2}
        >
          <Typography variant="body2">{t('reprovisionNodesFor')}</Typography>
          <YBRadioGroup
            className={classes.radioButtonGroup}
            orientation={RadioOrientation.Vertical}
            options={NODE_SELECTION_OPTIONS}
            value={nodeSelectionOption}
            onChange={handleNodeSelectionOptionChange}
            dataTestId={`${MODAL_NAME}-NodeSelection`}
          />
          <Box className={classes.nodeNameFieldContainer}>
            <YBAutoComplete
              value={
                (selectedNodeNameOption ??
                  universeNodeNameOptions[0] ?? {
                    label: '',
                    value: ''
                  }) as unknown as Record<string, string>
              }
              options={universeNodeNameOptions as unknown as Record<string, string>[]}
              getOptionLabel={getNodeNameOptionLabel}
              isOptionEqualToValue={(option, value) =>
                (option as NodeNameOption).value === (value as NodeNameOption).value
              }
              onChange={handleSelectedNodeChange}
              disableClearable
              disabled={isNodeNameFieldDisabled}
              ybInputProps={{
                dataTestId: `${MODAL_NAME}-NodeName`
              }}
              dataTestId={`${MODAL_NAME}-NodeName-Container`}
            />
          </Box>
          <div className={classes.footerNotes}>
            <MUILink
              href={REPROVISION_NODES_DOCUMENTATION_URL}
              target="_blank"
              rel="noopener noreferrer"
              className={classes.learnMoreLink}
              underline="none"
              data-testid={`${MODAL_NAME}-LearnMoreLink`}
            >
              <DocumentationIcon width={16} height={16} />
              <span>{t('learnMoreAboutOperationalConsiderations')}</span>
            </MUILink>
            <div
              className={clsx(classes.rollingRestartBanner, {
                [classes.rollingRestartBannerError]: !canRemainOnlineDuringRestart
              })}
            >
              {canRemainOnlineDuringRestart ? (
                <InfoIcon className={classes.rollingRestartBannerIcon} />
              ) : (
                <InfoErrorIcon className={classes.rollingRestartBannerIcon} />
              )}
              <Typography className={classes.rollingRestartBannerText} variant="body2">
                <Trans
                  t={t}
                  i18nKey={getRestartNoteI18nKey()}
                  values={{
                    universeName,
                    nodeName: selectedNodeNameOption?.value
                  }}
                  components={{ bold: <b /> }}
                />
              </Typography>
            </div>
          </div>
        </Box>
      </Box>
    </YBModal>
  );
};

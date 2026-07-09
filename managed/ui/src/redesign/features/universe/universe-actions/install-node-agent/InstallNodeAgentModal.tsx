import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useEffect, useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
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
import { NodeAgentAPI } from '../../../NodeAgent/api';
import { handleServerError } from '../../../../../utils/errorHandlingUtils';
import { api, universeQueryKey } from '../../../../helpers/api';
import { NODE_AGENT_PREREQ_DOCS_URL } from '../../../NodeAgent/constants';
import { YBBanner, YBBannerVariant } from '../../../../../components/common/descriptors';
import { YBExternalLink } from '../../../../components/YBLink/YBExternalLink';
import { YBErrorIndicator, YBLoading } from '../../../../../components/common/indicators';
import CheckIcon from '../../../../assets/check-white.svg';

import toastStyles from '../../../../../redesign/styles/toastStyles.module.scss';

interface InstallNodeAgentModalCommonProps {
  universeUuid: string;
  modalProps: YBModalProps;
  isReinstall: boolean;
}

type NodeNameOption = {
  label: string;
  value: string;
};

type InstallNodeAgentModalProps =
  | (InstallNodeAgentModalCommonProps & { isUniverseAction: true })
  | (InstallNodeAgentModalCommonProps & {
      nodeName: string;
      universeName: string;
      isUniverseAction: false;
    });

const useStyles = makeStyles((theme) => ({
  radioButtonGroup: {
    gap: theme.spacing(2)
  }
}));

const InstallationOption = {
  UNIVERSE: 'universe',
  NODE: 'node'
} as const;
type InstallationOption = (typeof InstallationOption)[keyof typeof InstallationOption];

const MODAL_NAME = 'InstallNodeAgentModal';
const TRANSLATION_KEY_PREFIX = 'nodeAgent.installNodeAgentModal';

export const InstallNodeAgentModal = (props: InstallNodeAgentModalProps) => {
  const { universeUuid, modalProps } = props;
  const [installOption, setInstallOption] = useState<InstallationOption>(
    props.isUniverseAction ? InstallationOption.UNIVERSE : InstallationOption.NODE
  );
  const [selectedNodeNameOption, setSelectedNodeNameOption] = useState<NodeNameOption>();
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const universeQuery = useQuery(
    universeQueryKey.detail(universeUuid),
    () => api.fetchUniverse(universeUuid),
    { enabled: props.isUniverseAction }
  );
  const universeNodeNames =
    universeQuery.data?.universeDetails.nodeDetailsSet
      .filter((nodeDetails) => !!nodeDetails.nodeName)
      .map((nodeDetails) => nodeDetails.nodeName as string)
      .sort((nodeNameA, nodeNameB) => nodeNameA?.localeCompare(nodeNameB)) ?? [];

  // Default the node dropdown to the first node when installing from universe actions.
  const firstNodeName = universeNodeNames[0];
  useEffect(() => {
    if (props.isUniverseAction && firstNodeName && !selectedNodeNameOption) {
      setSelectedNodeNameOption({ label: firstNodeName, value: firstNodeName });
    }
  }, [firstNodeName, props.isUniverseAction]);

  const getNodeNamesForInstall = (): string[] => {
    if (installOption === InstallationOption.UNIVERSE) {
      // The server will interpret an empty array as "all nodes in the universe".
      return [];
    }
    if (props.isUniverseAction) {
      return [selectedNodeNameOption?.value ?? ''];
    }
    return [props.nodeName];
  };

  const installNodeAgentMutation = useMutation(
    () =>
      NodeAgentAPI.installNodeAgent(universeUuid, {
        nodeNames: getNodeNamesForInstall()
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
        toast.success(
          <Typography variant="body2" component="span" className={toastStyles.toastMessage}>
            <CheckIcon />
            {t('success.requestSuccess')}
            <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
              {t('viewDetails', { keyPrefix: 'task' })}
            </a>
          </Typography>
        );
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
    installNodeAgentMutation.mutate(undefined /** variables */, { onSettled: () => resetModal() });
  };

  const handleInstallationOptionChange = (event: React.ChangeEvent<HTMLInputElement>): void => {
    // The type assertion is safe because we pass `INSTALLATION_OPTIONS` to the radio group
    // and it consists only of `InstallationOption` values.
    const value = event.target.value as InstallationOption;
    setInstallOption(value);
  };
  const handleSelectedNodeChange = (value: ValueType<NodeNameOption>) => {
    if (value && !Array.isArray(value)) {
      setSelectedNodeNameOption(value as NodeNameOption);
    }
  };

  const INSTALLATION_OPTIONS: OptionProps[] = [
    { value: InstallationOption.UNIVERSE, label: t('allNodeInUniverse') },
    {
      value: InstallationOption.NODE,
      label: props.isUniverseAction
        ? t('selectedNode')
        : t('specificSelectedNode', { nodeName: props.nodeName })
    }
  ];
  const universeNodeNameOptions = universeNodeNames.map((nodeName) => ({
    label: nodeName,
    value: nodeName
  }));

  const isNodeNameFieldDisabled = installOption === InstallationOption.UNIVERSE;
  const isSubmitDisabled =
    props.isUniverseAction &&
    installOption === InstallationOption.NODE &&
    !selectedNodeNameOption?.value;
  const universeName = props.isUniverseAction ? universeQuery.data?.name : props.universeName;

  const installActionI18nKeySuffix = props.isReinstall ? 'reinstall' : 'install';
  const modalTitle = t(`title.${installActionI18nKeySuffix}`);
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (universeQuery.isLoading) {
    return (
      <YBModal
        title={modalTitle}
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
        title={modalTitle}
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
            universeUuid: props.universeUuid
          })}
        />
      </YBModal>
    );
  }

  return (
    <YBModal
      title={modalTitle}
      submitLabel={t(`confirmButton.${installActionI18nKeySuffix}`)}
      cancelLabel={cancelLabel}
      onSubmit={onSubmit}
      isSubmitting={isSubmitting}
      buttonProps={{ primary: { disabled: isSubmitDisabled } }}
      size="md"
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" height="100%">
        <YBBanner variant={YBBannerVariant.INFO}>
          <Typography variant="body1" gutterBottom>
            {t('beforeYouStart')}
          </Typography>
          <Typography variant="body2">
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.prerequisitesInfo`}
              components={{
                nodeAgentPrereqDocsLink: <YBExternalLink href={NODE_AGENT_PREREQ_DOCS_URL} />
              }}
              values={{ universeName }}
            />
          </Typography>
        </YBBanner>
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} marginTop={3}>
          <Typography variant="body2">
            {t(`installNodeAgentFor.${installActionI18nKeySuffix}`)}
          </Typography>
          <YBRadioGroup
            className={classes.radioButtonGroup}
            orientation={RadioGroupOrientation.VERTICAL}
            options={INSTALLATION_OPTIONS}
            value={installOption}
            onChange={handleInstallationOptionChange}
          />
          {props.isUniverseAction && (
            <Box width={340} maxWidth="100%" marginLeft={3}>
              <Select
                value={selectedNodeNameOption}
                onChange={handleSelectedNodeChange}
                options={universeNodeNameOptions}
                isDisabled={isNodeNameFieldDisabled}
                isClearable={false}
              />
            </Box>
          )}
        </Box>
      </Box>
    </YBModal>
  );
};

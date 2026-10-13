import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { RadioOrientation, YBAutoComplete, YBRadioGroup } from '@yugabyte-ui-library/core';
import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';

import { YBModal, YBModalProps } from '../../../../components';
import { fetchTaskUntilItCompletes } from '../../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../../utils/errorHandlingUtils';
import { api, QUERY_KEY, providerQueryKey, universeQueryKey } from '../../../../helpers/api';
import { NODE_AGENT_PREREQ_DOCS_URL } from '../../../NodeAgent/constants';
import { YBBanner, YBBannerVariant } from '../../../../../components/common/descriptors';
import { YBExternalLink } from '../../../../components/YBLink/YBExternalLink';
import { YBErrorIndicator, YBLoading } from '../../../../../components/common/indicators';
import { Certificate, Universe } from '../../../../helpers/dtos';
import { getPrimaryCluster } from '../../../../../utils/universeUtilsTyped';
import { ProviderCode } from '../../../../../components/configRedesign/providerRedesign/constants';
import { YBProvider } from '../../../../../components/configRedesign/providerRedesign/types';
import { upgradeNodeAgent } from '@app/v2/api/node-agent/node-agent';
import type {
  NodeAgentUpgradeReqBody,
  YBATaskRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import CheckIcon from '../../../../assets/check-white.svg';

import toastStyles from '../../../../../redesign/styles/toastStyles.module.scss';

interface UpdateNodeAgentModalCommonProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

type SelectOption = {
  label: string;
  value: string;
};

const toSelectOption = (value: string): SelectOption => ({ label: value, value });

type UpdateNodeAgentModalProps =
  | (UpdateNodeAgentModalCommonProps & { isUniverseAction: true })
  | (UpdateNodeAgentModalCommonProps & {
      nodeName: string;
      universeName: string;
      isUniverseAction: false;
    });

type UpdateNodeAgentFormProps = UpdateNodeAgentModalProps & {
  universe: Universe;
  certificates: Certificate[];
  provider: YBProvider | undefined;
};

const CERT_TYPE_SELF_SIGNED = 'SelfSigned';
const CERT_TYPE_CUSTOM_HOST_PATH = 'CustomCertHostPath';

const useStyles = makeStyles((theme) => ({
  radioButtonGroup: {
    gap: theme.spacing(2)
  },
  selectFieldContainer: {
    width: 340,
    maxWidth: '100%',
    marginLeft: theme.spacing(4)
  }
}));

const CertificateOption = {
  EXISTING: 'existing',
  CUSTOM: 'custom'
} as const;
type CertificateOption = (typeof CertificateOption)[keyof typeof CertificateOption];

const UpdateOption = {
  UNIVERSE: 'universe',
  NODE: 'node'
} as const;
type UpdateOption = (typeof UpdateOption)[keyof typeof UpdateOption];

const MODAL_NAME = 'UpdateNodeAgentModal';
const TRANSLATION_KEY_PREFIX = 'nodeAgent.updateNodeAgentModal';

const getOptionLabel = (option: string | Record<string, string>): string =>
  typeof option === 'string' ? option : option.label;

const getUniverseNodeNames = (universe: Universe): string[] =>
  universe.universeDetails.nodeDetailsSet
    .filter((nodeDetails) => !!nodeDetails.nodeName)
    .map((nodeDetails) => nodeDetails.nodeName as string)
    .sort((nodeNameA, nodeNameB) => nodeNameA?.localeCompare(nodeNameB));

export const UpdateNodeAgentModal = (props: UpdateNodeAgentModalProps) => {
  const { universeUuid, modalProps } = props;
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });

  const universeQuery = useQuery(universeQueryKey.detail(universeUuid), () =>
    api.fetchUniverse(universeUuid)
  );
  const certificatesQuery = useQuery(QUERY_KEY.getCertificates, api.getCertificates);
  const providerUuid = getPrimaryCluster(universeQuery.data?.universeDetails.clusters ?? [])
    ?.userIntent?.provider;
  const providerQuery = useQuery(
    providerQueryKey.detail(providerUuid ?? ''),
    () => api.fetchProvider(providerUuid),
    { enabled: !!providerUuid }
  );

  if (
    universeQuery.isLoading ||
    certificatesQuery.isLoading ||
    (providerUuid && providerQuery.isLoading)
  ) {
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

  if (universeQuery.isError || !universeQuery.data) {
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
    <UpdateNodeAgentForm
      {...props}
      universe={universeQuery.data}
      certificates={certificatesQuery.data ?? []}
      provider={providerQuery.data}
    />
  );
};

const UpdateNodeAgentForm = (props: UpdateNodeAgentFormProps) => {
  const { universeUuid, modalProps, universe, certificates, provider } = props;
  const universeNodeNames = getUniverseNodeNames(universe);
  const firstNodeName = universeNodeNames[0];
  const [updateOption, setUpdateOption] = useState<UpdateOption>(
    props.isUniverseAction ? UpdateOption.UNIVERSE : UpdateOption.NODE
  );
  const [selectedNodeNameOption, setSelectedNodeNameOption] = useState<SelectOption | undefined>(
    props.isUniverseAction && firstNodeName ? toSelectOption(firstNodeName) : undefined
  );
  const [selectedCertificateName, setSelectedCertificateName] = useState<string>();
  const [certificateOption, setCertificateOption] = useState<CertificateOption>(
    CertificateOption.EXISTING
  );
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const isOnPrem = provider?.code === ProviderCode.ON_PREM;
  const certificateNames = certificates
    .filter((certificate: Certificate) => {
      if (certificate.certType === CERT_TYPE_SELF_SIGNED) {
        return true;
      }
      // CustomCertHostPath is only valid for on-prem providers.
      return certificate.certType === CERT_TYPE_CUSTOM_HOST_PATH && isOnPrem;
    })
    .map((certificate) => certificate.label);
  const firstCertificateName = certificateNames[0];
  const certificateOptions = certificateNames.map(toSelectOption);

  const getNodeNamesForUpdate = (): string[] => {
    if (updateOption === UpdateOption.UNIVERSE) {
      return [];
    }
    if (props.isUniverseAction) {
      return [selectedNodeNameOption?.value ?? ''];
    }
    return [props.nodeName];
  };

  const updateNodeAgentMutation = useMutation(
    (data: NodeAgentUpgradeReqBody) => upgradeNodeAgent(universeUuid, data),
    {
      onSuccess: (response: YBATaskRespResponse) => {
        const taskUuid = response.task_uuid;
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <Typography variant="body2" className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                {t('error.taskFailure')}
                <a href={`/tasks/${taskUuid}`} rel="noopener noreferrer" target="_blank">
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
            <a href={`/tasks/${taskUuid}`} rel="noopener noreferrer" target="_blank">
              {t('viewDetails', { keyPrefix: 'task' })}
            </a>
          </Typography>
        );
        if (taskUuid) {
          fetchTaskUntilItCompletes(taskUuid, handleTaskCompletion);
        }
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
    const payload: NodeAgentUpgradeReqBody = {
      node_names: getNodeNamesForUpdate(),
      certs_only: true
    };
    if (certificateOption === CertificateOption.CUSTOM && selectedCertificateName) {
      payload.certificate_name = selectedCertificateName;
    }
    updateNodeAgentMutation.mutate(payload, { onSettled: () => resetModal() });
  };

  const handleUpdateOptionChange = (event: React.ChangeEvent<HTMLInputElement>): void => {
    const value = event.target.value as UpdateOption;
    setUpdateOption(value);
  };
  const handleSelectedNodeChange = (
    _event: React.ChangeEvent<{}>,
    option: string | Record<string, string> | (string | Record<string, string>)[] | null
  ) => {
    if (option && !Array.isArray(option) && typeof option !== 'string') {
      setSelectedNodeNameOption(option as SelectOption);
    }
  };
  const handleCertificateOptionChange = (event: React.ChangeEvent<HTMLInputElement>): void => {
    const value = event.target.value as CertificateOption;
    setCertificateOption(value);
    if (value === CertificateOption.EXISTING) {
      setSelectedCertificateName(undefined);
    } else if (value === CertificateOption.CUSTOM) {
      setSelectedCertificateName(firstCertificateName);
    }
  };
  const handleSelectedCertificateChange = (
    _event: React.ChangeEvent<{}>,
    option: string | Record<string, string> | (string | Record<string, string>)[] | null
  ) => {
    if (option && !Array.isArray(option) && typeof option !== 'string') {
      setSelectedCertificateName((option as SelectOption).value);
    }
  };

  const UPDATE_OPTIONS = [
    { value: UpdateOption.UNIVERSE, label: t('allNodeInUniverse') },
    {
      value: UpdateOption.NODE,
      label: props.isUniverseAction
        ? t('selectedNode')
        : t('specificSelectedNode', { nodeName: props.nodeName })
    }
  ];
  const universeNodeNameOptions = universeNodeNames.map(toSelectOption);

  const CERTIFICATE_OPTIONS = [
    { value: CertificateOption.EXISTING, label: t('certificateOption.existing') },
    { value: CertificateOption.CUSTOM, label: t('certificateOption.custom') }
  ];
  const isCustomCertificateSelected = certificateOption === CertificateOption.CUSTOM;
  const isNodeNameFieldDisabled = updateOption === UpdateOption.UNIVERSE;
  const isSubmitDisabled =
    (props.isUniverseAction &&
      updateOption === UpdateOption.NODE &&
      !selectedNodeNameOption?.value) ||
    (isCustomCertificateSelected && !selectedCertificateName);
  const universeName = props.isUniverseAction ? universe.name : props.universeName;

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('confirmButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={onSubmit}
      isSubmitting={isSubmitting}
      buttonProps={{ primary: { disabled: isSubmitDisabled } }}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      size="md"
      dialogContentProps={{ dividers: true }}
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
          <Typography variant="body2">{t('updateNodeAgentFor')}</Typography>
          <YBRadioGroup
            className={classes.radioButtonGroup}
            orientation={RadioOrientation.Vertical}
            options={UPDATE_OPTIONS}
            value={updateOption}
            onChange={handleUpdateOptionChange}
            dataTestId={`${MODAL_NAME}-UpdateOption`}
          />
          {props.isUniverseAction && (
            <Box className={classes.selectFieldContainer}>
              <YBAutoComplete
                value={
                  (selectedNodeNameOption ??
                    universeNodeNameOptions[0] ?? {
                      label: '',
                      value: ''
                    }) as unknown as Record<string, string>
                }
                options={universeNodeNameOptions as unknown as Record<string, string>[]}
                getOptionLabel={getOptionLabel}
                isOptionEqualToValue={(option, value) =>
                  (option as SelectOption).value === (value as SelectOption).value
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
          )}
          <Typography variant="body2">{t('certificateLabel')}</Typography>
          <YBRadioGroup
            className={classes.radioButtonGroup}
            orientation={RadioOrientation.Vertical}
            options={CERTIFICATE_OPTIONS}
            value={certificateOption}
            onChange={handleCertificateOptionChange}
            dataTestId={`${MODAL_NAME}-CertificateOption`}
          />
          {isCustomCertificateSelected && (
            <Box className={classes.selectFieldContainer}>
              <YBAutoComplete
                value={
                  (selectedCertificateName
                    ? toSelectOption(selectedCertificateName)
                    : {
                        label: '',
                        value: ''
                      }) as unknown as Record<string, string>
                }
                options={certificateOptions as unknown as Record<string, string>[]}
                getOptionLabel={getOptionLabel}
                isOptionEqualToValue={(option, value) =>
                  (option as SelectOption).value === (value as SelectOption).value
                }
                onChange={handleSelectedCertificateChange}
                disableClearable
                ybInputProps={{
                  placeholder: t('certificatePlaceholder'),
                  dataTestId: `${MODAL_NAME}-Certificate`
                }}
                dataTestId={`${MODAL_NAME}-Certificate-Container`}
              />
            </Box>
          )}
        </Box>
      </Box>
    </YBModal>
  );
};

import React, { FC, useEffect } from 'react';
import clsx from 'clsx';
import { useMutation, useQuery } from 'react-query';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Tabs, Tab, useTheme } from '@material-ui/core';
import { toast } from 'react-toastify';
import { YBAlert, YBModal, YBToggleField, YBTooltip, AlertVariant } from '../../../../components';
import {
  TOAST_AUTO_DISMISS_INTERVAL,
  EIT_FIELD_NAME,
  ROTATE_NODE_NODE_CERT_FIELD_NAME,
  ROTATE_CLIENT_NODE_CERT_FIELD_NAME,
  NODE_NODE_CERT_FIELD_NAME,
  CLIENT_NODE_CERT_FIELD_NAME,
  ENABLE_NODE_NODE_ENCRYPTION_NAME,
  ENABLE_CLIENT_NODE_ENCRYPTION_NAME,
  FORM_RESET_VALUES,
  EncryptionInTransitFormValues,
  useEITStyles,
  getInitialFormValues,
  isSelfSignedCert,
  USE_SAME_CERTS_FIELD_NAME
} from './EncryptionInTransitUtils';
import { api, QUERY_KEY } from '../../../../utils/api';
import { Certificate } from '../../universe-form/utils/dto';
import { CertificateAuthority } from './components/CertificateAuthority';
import { RotateServerCerts } from './components/RotateServerCerts';
import { RollingUpgrade } from './components/RollingUpgrade';
import { YBLoading } from '../../../../../components/common/indicators';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { createErrorMessage } from '../../universe-form/utils/helpers';
import { getXClusterConfigUuids } from '../../../../../components/xcluster/ReplicationUtils';
import { Universe } from '../../../../helpers/dtos';

//EAR Component
interface EncryptionInTransitProps {
  open: boolean;
  onClose: () => void;
  universe: Universe;
}

enum EitTabs {
  'CACert' = 'CACert',
  'ServerCert' = 'ServerCert'
}

const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

const NonRollingBanner: FC = () => {
  const { t } = useTranslation();
  return (
    <Box mt={2}>
      <YBAlert
        text={t('universeActions.encryptionInTransit.disableEITWarning')}
        variant={AlertVariant.Warning}
        open={true}
      />
    </Box>
  );
};

export const EncryptionInTransit: FC<EncryptionInTransitProps> = ({ open, onClose, universe }) => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const theme = useTheme();
  //universe current status
  const { universeDetails } = universe;
  const universeId = universe.universeUUID;

  //prefetch data
  const { isLoading, data: certificates } = useQuery(
    QUERY_KEY.getCertificates,
    api.getCertificates
  );

  //initialize form
  const INITIAL_VALUES = getInitialFormValues(universeDetails);
  const formMethods = useForm<EncryptionInTransitFormValues>({
    defaultValues: INITIAL_VALUES,
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, watch, handleSubmit, setValue } = formMethods;

  //initial values
  const encryptionEnabled = INITIAL_VALUES.enableUniverseEncryption;
  const enableNodeToNodeEncryptInitial = INITIAL_VALUES.enableNodeToNodeEncrypt;
  const enableClientToNodeEncryptInitial = INITIAL_VALUES.enableClientToNodeEncrypt;
  const clientRootCAInitial = INITIAL_VALUES.clientRootCA;
  const rootCAInitial = INITIAL_VALUES.rootCA;

  //watch field values
  const enableUniverseEncryption = watch(EIT_FIELD_NAME);
  const enableNodeToNodeEncrypt = watch(ENABLE_NODE_NODE_ENCRYPTION_NAME);
  const enableClientToNodeEncrypt = watch(ENABLE_CLIENT_NODE_ENCRYPTION_NAME);
  const rotateNToN = watch(ROTATE_NODE_NODE_CERT_FIELD_NAME);
  const rotateCToN = watch(ROTATE_CLIENT_NODE_CERT_FIELD_NAME);
  const rootCA = watch(NODE_NODE_CERT_FIELD_NAME);
  const clientRootCA = watch(CLIENT_NODE_CERT_FIELD_NAME);
  const rootAndClientRootCASame = watch(USE_SAME_CERTS_FIELD_NAME);

  //Disable encryption in transit toggle if (a)->one of server cert rotations enabled OR (b)->one of root certs are modiified
  const disableEITToggle =
    rotateNToN ||
    rotateCToN ||
    (encryptionEnabled && (rootCA !== rootCAInitial || clientRootCA !== clientRootCAInitial));
  //EIT or CNEncrytion or NNEncryption toggle is turned on or off
  const tlsToggled =
    encryptionEnabled !== enableUniverseEncryption ||
    enableNodeToNodeEncryptInitial !== enableNodeToNodeEncrypt ||
    enableClientToNodeEncryptInitial !== enableClientToNodeEncrypt;

  //Server cert rotation is only supported for self signed certs
  const rootCAInfo = certificates?.find((cert: Certificate) => cert.uuid === rootCA);
  const clientRootCAInfo = certificates?.find((cert: Certificate) => cert.uuid === clientRootCA);
  const disableServerCertRotation =
    !encryptionEnabled ||
    !(enableNodeToNodeEncrypt || enableClientToNodeEncrypt) ||
    (rootCAInfo && !isSelfSignedCert(rootCAInfo)) ||
    (!rootAndClientRootCASame && clientRootCAInfo && !isSelfSignedCert(clientRootCAInfo));

  const [currentTab, setTab] = React.useState('');

  //methods
  const handleChange = (_: any, tab: string) => setTab(tab);

  const setEIT = useMutation(
    (payload: Partial<EncryptionInTransitFormValues>) => api.updateTLS(universeId, payload),
    {
      onSuccess: () => {
        onClose();
      },
      onError: (e) => {
        toast.error(createErrorMessage(e), TOAST_OPTIONS);
      }
    }
  );

  const constructPayload = (values: EncryptionInTransitFormValues) => {
    let { enableUniverseEncryption, rollingUpgrade, upgradeDelay, ...payload } = values;

    if (
      !values.enableUniverseEncryption ||
      (values.rootAndClientRootCASame && values.enableNodeToNodeEncrypt === false)
    )
      payload = { ...payload, ...FORM_RESET_VALUES };

    if (values.enableNodeToNodeEncrypt === false) {
      payload['rootCA'] = null;
      payload['createNewRootCA'] = false;
    }
    if (values.enableNodeToNodeEncrypt === true) {
      if (!values['rootCA']) {
        payload['rootCA'] = null;
        payload['createNewRootCA'] = true;
      } else {
        payload['createNewRootCA'] = false;
      }
    }

    if (values.enableClientToNodeEncrypt === false) {
      payload['clientRootCA'] = null;
      payload['createNewClientRootCA'] = false;
    }
    if (values.enableClientToNodeEncrypt === true && !values.rootAndClientRootCASame) {
      if (!values['clientRootCA']) {
        payload['clientRootCA'] = null;
        payload['createNewClientRootCA'] = true;
      } else {
        payload['createNewClientRootCA'] = false;
      }
    }

    if (values.rootAndClientRootCASame) {
      if (values.enableNodeToNodeEncrypt && values.enableClientToNodeEncrypt) {
        payload['clientRootCA'] = null;
        payload['createNewClientRootCA'] = false;
      }
    }

    //Rolling Upgrade
    if (values.rollingUpgrade && !tlsToggled) {
      payload.sleepAfterMasterRestartMillis = values.upgradeDelay * 1000;
      payload.sleepAfterTServerRestartMillis = values.upgradeDelay * 1000;
      payload.upgradeOption = 'Rolling';
    } else {
      payload.upgradeOption = 'Non-Rolling';
    }

    return payload;
  };

  const handleFormSubmit = handleSubmit(async (values) => {
    if (
      values.enableUniverseEncryption &&
      !(values.enableNodeToNodeEncrypt || values.enableClientToNodeEncrypt)
    ) {
      //If encryption is enabled at global level, but if one of NN OR CN toggle is not turned on
      toast.warn(t('universeActions.encryptionInTransit.enableEITWarning'), TOAST_OPTIONS);
    } else {
      try {
        let payload = constructPayload(values);
        setEIT.mutateAsync(payload);
      } catch (e) {
        console.error(e);
      }
    }
  });

  const canEditEAT = hasNecessaryPerm({
    onResource: universeId,
    ...ApiPermissionMap.MODIFY_UNIVERSE_TLS
  });
  useEffect(() => {
    if (disableServerCertRotation) setTab(EitTabs.CACert);
  }, [setTab, disableServerCertRotation]);

  useEffect(() => {
    if (!isLoading && !currentTab)
      setTab(disableServerCertRotation ? EitTabs.CACert : EitTabs.ServerCert);
  }, [isLoading, disableServerCertRotation, setTab, currentTab]);

  const isNodeToNodeCaCertChange = rootCA != INITIAL_VALUES.rootCA;
  const { sourceXClusterConfigs, targetXClusterConfigs } = getXClusterConfigUuids(universe);
  const universeHasXClusterConfig =
    sourceXClusterConfigs.length > 0 || targetXClusterConfigs.length > 0;
  return (
    <YBModal
      open={open}
      titleSeparator
      size="md"
      overrideWidth={650}
      overrideHeight="auto"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.apply')}
      title={t('universeActions.encryptionInTransit.title')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      submitTestId="EncryptionInTransit-Submit"
      cancelTestId="EncryptionInTransit-Close"
      buttonProps={{
        primary: {
          disabled: !canEditEAT
        }
      }}
      submitButtonTooltip={!canEditEAT ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      {isLoading ? (
        <YBLoading text={' '} />
      ) : (
        <FormProvider {...formMethods}>
          <Box
            mb={4}
            mt={2}
            px={1}
            display="flex"
            width="100%"
            flexDirection="column"
            data-testid="EncryptionInTransit-Modal"
          >
            <Box
              display="flex"
              flexDirection="column"
              className={clsx(classes.enableEITContainer, classes.container)}
            >
              <Box display="flex" alignItems="center" justifyContent="space-between">
                <Box>
                  <Typography variant="h6">
                    {t('universeActions.encryptionInTransit.enableEIT')}
                  </Typography>
                </Box>
                <Box>
                  <YBTooltip
                    title={
                      disableEITToggle
                        ? t('universeActions.encryptionInTransit.disableEncryptionWarning')
                        : ''
                    }
                    placement="top"
                  >
                    <span>
                      <YBToggleField
                        name={EIT_FIELD_NAME}
                        onChange={(e) => {
                          setValue(EIT_FIELD_NAME, e.target.checked);
                          if (!encryptionEnabled && e.target.checked) {
                            setValue(ENABLE_NODE_NODE_ENCRYPTION_NAME, e.target.checked);
                            setValue(ENABLE_CLIENT_NODE_ENCRYPTION_NAME, e.target.checked);
                          }
                        }}
                        inputProps={{
                          'data-testid': 'EnableEncryptionInTransit-Toggle'
                        }}
                        control={control}
                        disabled={disableEITToggle}
                      />
                    </span>
                  </YBTooltip>
                </Box>
              </Box>
            </Box>

            {enableUniverseEncryption && (
              <Box mt={4} className={classes.eitTabContainer}>
                <Tabs
                  value={disableServerCertRotation ? EitTabs.CACert : currentTab}
                  indicatorColor="primary"
                  textColor="primary"
                  onChange={handleChange}
                  aria-label="tab section example"
                  className={classes.tab}
                >
                  <Tab
                    label={t('universeActions.encryptionInTransit.certAuthority')}
                    value={EitTabs.CACert}
                  />
                  {!disableServerCertRotation && (
                    <Tab
                      label={t('universeActions.encryptionInTransit.serverCert')}
                      value={EitTabs.ServerCert}
                    />
                  )}
                </Tabs>

                {(currentTab === EitTabs.CACert || disableServerCertRotation) && (
                  <CertificateAuthority initialValues={INITIAL_VALUES} />
                )}
                {currentTab === EitTabs.ServerCert && !disableServerCertRotation && (
                  <RotateServerCerts initialValues={INITIAL_VALUES} />
                )}

                {!tlsToggled && <RollingUpgrade />}
              </Box>
            )}
            <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} marginTop={2}>
              {tlsToggled && <NonRollingBanner />}
              {((universeHasXClusterConfig && tlsToggled) ||
                (isNodeToNodeCaCertChange && sourceXClusterConfigs.length > 0)) && (
                <YBAlert
                  text={t(
                    `universeActions.encryptionInTransit.${
                      tlsToggled ? 'xClusterToggleEncryptionWarning' : 'xClusterRotateCaWarning'
                    }`
                  )}
                  variant={AlertVariant.Warning}
                  open={true}
                />
              )}
            </Box>
          </Box>
        </FormProvider>
      )}
    </YBModal>
  );
};

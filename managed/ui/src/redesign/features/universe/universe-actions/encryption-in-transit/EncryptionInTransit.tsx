import React, { FC } from 'react';
import clsx from 'clsx';
import { useMutation, useQuery } from 'react-query';
import { useForm, FormProvider } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Typography, Tabs, Tab } from '@material-ui/core';
import { toast } from 'react-toastify';
import { YBModal, YBToggleField, YBTooltip } from '../../../../components';
import {
  TOAST_AUTO_DISMISS_INTERVAL,
  EIT_FIELD_NAME,
  ROTATE_NODE_NODE_CERT_FIELD_NAME,
  ROTATE_CLIENT_NODE_CERT_FIELD_NAME,
  NODE_NODE_CERT_FIELD_NAME,
  CLIENT_NODE_CERT_FIELD_NAME,
  FORM_RESET_VALUES,
  EncryptionInTransitFormValues,
  useEITStyles,
  getInitialFormValues
} from './EncryptionInTransitUtils';
import { api, QUERY_KEY } from '../../../../utils/api';
import { Universe } from '../../universe-form/utils/dto';
import { CertificateAuthority } from './components/CertificateAuthority';
import { RotateServerCerts } from './components/RotateServerCerts';
import { RollingUpgrade } from './components/RollingUpgrade';
import { YBLoading } from '../../../../../components/common/indicators';

//EAR Component
interface EncryptionInTransitProps {
  open: boolean;
  onClose: () => void;
  universe: Universe;
}

const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };

export const EncryptionInTransit: FC<EncryptionInTransitProps> = ({ open, onClose, universe }) => {
  const { t } = useTranslation();
  const classes = useEITStyles();
  const [currentTab, setTab] = React.useState<string>('CACert');

  //universe current status
  const { universeDetails } = universe;
  const universeId = universe.universeUUID;

  //prefetch data
  const { isLoading } = useQuery(QUERY_KEY.getCertificates, api.getCertificates);

  //initialize form
  const INITIAL_VALUES = getInitialFormValues(universeDetails);
  const formMethods = useForm<EncryptionInTransitFormValues>({
    defaultValues: INITIAL_VALUES,
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, watch, handleSubmit } = formMethods;

  //initial values
  const encryptionEnabled = INITIAL_VALUES.enableUniverseEncryption;
  const clientRootCAInitial = INITIAL_VALUES.clientRootCA;
  const rootCAInitial = INITIAL_VALUES.rootCA;

  //watch field values
  const enableUniverseEncryption = watch(EIT_FIELD_NAME);
  const rotateNToN = watch(ROTATE_NODE_NODE_CERT_FIELD_NAME);
  const rotateCToN = watch(ROTATE_CLIENT_NODE_CERT_FIELD_NAME);
  const rootCA = watch(NODE_NODE_CERT_FIELD_NAME);
  const clientRootCA = watch(CLIENT_NODE_CERT_FIELD_NAME);

  const disableEITToggle =
    rotateNToN ||
    rotateCToN ||
    (encryptionEnabled && (rootCA !== rootCAInitial || clientRootCA !== clientRootCAInitial));

  //methods
  const handleChange = (_: any, tab: string) => setTab(tab);

  const setEIT = useMutation(
    (payload: Partial<EncryptionInTransitFormValues>) => api.updateTLS(universeId, payload),
    {
      onSuccess: () => {
        onClose();
      },
      onError: () => {
        toast.error(t('common.genericFailure'), TOAST_OPTIONS);
      }
    }
  );

  const constructPayload = (values: EncryptionInTransitFormValues) => {
    let { enableUniverseEncryption, rollingUpgrade, upgradeDelay, ...payload } = values;
    if (!values.enableUniverseEncryption) payload = { ...payload, ...FORM_RESET_VALUES };

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
    if (values.rollingUpgrade) {
      payload.sleepAfterMasterRestartMillis = values.upgradeDelay * 1000;
      payload.sleepAfterTServerRestartMillis = values.upgradeDelay * 1000;
      payload.upgradeOption = 'Rolling';
    } else {
      payload.upgradeOption = 'Non-Rolling';
    }

    return payload;
  };

  const handleFormSubmit = handleSubmit(async (values) => {
    try {
      let payload = constructPayload(values);
      setEIT.mutateAsync(payload);
    } catch (e) {
      console.error(e);
    }
  });

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
      submitTestId="EncryptionAtRest-Submit"
      cancelTestId="EncryptionAtRest-Close"
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
            data-testid="EncryptionAtRest-Modal"
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
              <Box mt={4}>
                <Tabs
                  value={currentTab}
                  indicatorColor="primary"
                  textColor="primary"
                  onChange={handleChange}
                  aria-label="tab section example"
                  className={classes.tab}
                >
                  <Tab
                    label={t('universeActions.encryptionInTransit.certAuthority')}
                    value="CACert"
                  />
                  {encryptionEnabled && (
                    <Tab
                      label={t('universeActions.encryptionInTransit.serverCert')}
                      value="ServerCert"
                    />
                  )}
                </Tabs>
                {currentTab === 'CACert' && <CertificateAuthority initialValues={INITIAL_VALUES} />}
                {currentTab === 'ServerCert' && (
                  <RotateServerCerts initialValues={INITIAL_VALUES} />
                )}
                <RollingUpgrade />
              </Box>
            )}
          </Box>
        </FormProvider>
      )}
    </YBModal>
  );
};

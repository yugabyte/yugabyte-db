import { useEffect, useMemo } from 'react';
import { isEmpty } from 'lodash';
import { FormProvider, useForm } from 'react-hook-form';
import { yupResolver } from '@hookform/resolvers/yup';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { AlertVariant, mui, yba, YBAlert } from '@yugabyte-ui-library/core';
import { DeploymentPortsField } from '../../create-universe/fields';
import { useEditUniverse } from '../../../../../v2/api/universe/universe';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import {
  mapAPIPortValues,
  mapCommunicationPorts
} from '../../create-universe/utils/createUniversePayload';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { OtherAdvancedProps } from '../../create-universe/steps/advanced-settings/dtos';
import { OtherAdvancedValidationSchema } from '../../create-universe/steps/advanced-settings/ValidationSchema';
import { CloudType } from '@app/redesign/helpers/dtos';
import { FullMoveWarning } from '../components';

const { YBModal } = yba;
const { styled, Box, boxClasses } = mui;

const ModalContent = styled(Box)(({ theme }) => ({
  [`.yb-${boxClasses.root}`]: {
    width: '100%'
  }
}));

interface EditNetworkPortsModalProps {
  open: boolean;
  onClose: () => void;
}

export const EditNetworkPortsModal = ({ open, onClose }: EditNetworkPortsModalProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings.deployPortsFeild'
  });
  const { t: tSettings } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });
  const { universeData } = useEditUniverseContext();
  const editUniverse = useEditUniverse();
  const universeUUID = universeData?.info?.universe_uuid;
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const enableYSQL = universeData?.spec?.ysql?.enable;
  const enableYCQL = universeData?.spec?.ycql?.enable;
  const enableCP = universeData?.spec?.ysql?.enable_connection_pooling;
  const networkingSpec = universeData?.spec?.networking_spec;
  const communicationPorts = universeData?.spec?.networking_spec?.communication_ports;
  const defaultValues = mapAPIPortValues(communicationPorts ?? {});

  const validationSchema = useMemo(
    () =>
      OtherAdvancedValidationSchema(tSettings, {
        providerCode: providerCode as CloudType | undefined,
        requireAccessKey: false,
        ysql: !!enableYSQL,
        ycql: !!enableYCQL,
        enableConnectionPooling: !!enableCP
      }),
    [tSettings, providerCode, enableYSQL, enableYCQL, enableCP]
  );

  const methods = useForm<Partial<OtherAdvancedProps>>({
    defaultValues,
    resolver: yupResolver(validationSchema),
    mode: 'onSubmit',
    reValidateMode: 'onChange'
  });
  const {
    handleSubmit,
    reset,
    formState: { isDirty, errors, isSubmitted }
  } = methods;
  const hasErrors = !isEmpty(errors);

  useEffect(() => {
    if (open) reset(defaultValues);
  }, [open]);

  const handleFormSubmit = handleSubmit(async (values) => {
    if (!universeUUID || !primaryCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }
    const updatedPortsValues = mapCommunicationPorts(values);
    editUniverse.mutate(
      {
        uniUUID: universeUUID,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: primaryCluster.uuid
            }
          ],
          networking_spec: {
            ...networkingSpec,
            communication_ports: { ...communicationPorts, ...updatedPortsValues }
          }
        }
      },
      {
        onSuccess: (response) => {
          reset(values);
          handleEditUniverseSuccess(response.task_uuid);
          onClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('networkPorts', { keyPrefix: 'editUniverse.advanced' })}
      submitLabel={t('apply', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      titleSeparator
      size="md"
      scroll="body"
      dialogContentProps={{ sx: { padding: '16px !important', overflow: 'visible' } }}
      overrideHeight={'fit-content'}
      onSubmit={handleFormSubmit}
    >
      <FormProvider {...methods}>
        <ModalContent>
          <DeploymentPortsField
            ysql={!!enableYSQL}
            ycql={!!enableYCQL}
            enableConnectionPooling={enableCP}
            providerCode={providerCode ?? ''}
            isEditMode={true}
          />
          {isSubmitted && hasErrors && (
            <Box mt={2}>
              <YBAlert
                open
                variant={AlertVariant.Error}
                text={<Trans t={tSettings}>{tSettings('validation.alertMsg')}</Trans>}
              />
            </Box>
          )}
          {isDirty && <FullMoveWarning setting="networkPorts" />}
        </ModalContent>
      </FormProvider>
    </YBModal>
  );
};

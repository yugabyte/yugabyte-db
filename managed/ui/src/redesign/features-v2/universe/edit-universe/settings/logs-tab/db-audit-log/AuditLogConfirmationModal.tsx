import { FC } from 'react';

import { YBModalProps } from '@app/redesign/components';
import {
  TelemetryConfigConfirmationModal,
  TelemetryConfigConfirmationOperation
} from '../../shared/TelemetryConfigConfirmationModal';
import { AuditLogOperation } from './auditLogHelpers';

interface AuditLogConfirmationModalProps {
  operation: AuditLogOperation;
  universeName: string;
  replicationFactor: number;
  isSubmitting: boolean;
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const mapOperation = (operation: AuditLogOperation): TelemetryConfigConfirmationOperation =>
  operation === 'create' ? 'enable' : 'edit';

export const AuditLogConfirmationModal: FC<AuditLogConfirmationModalProps> = ({
  operation,
  universeName,
  replicationFactor,
  isSubmitting,
  onSubmit,
  modalProps
}) => (
  <TelemetryConfigConfirmationModal
    configType="auditLog"
    operation={mapOperation(operation)}
    universeName={universeName}
    replicationFactor={replicationFactor}
    isSubmitting={isSubmitting}
    onSubmit={onSubmit}
    modalProps={modalProps}
  />
);

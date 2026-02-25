import _ from 'lodash';
import { Theme, makeStyles } from '@material-ui/core';

import { getPrimaryCluster } from '../../../../../utils/universeUtilsTyped';

import { Certificate } from '../../universe-form/utils/dto';
import { UniverseDetails } from '../../../../helpers/dtos';

//styles
export const useEITStyles = makeStyles((theme: Theme) => ({
  container: {
    backgroundColor: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1)
  },
  enableEITContainer: {
    padding: theme.spacing(2),
    '& .MuiFormControlLabel-root': {
      marginRight: 0
    }
  },
  eitTabContainer: {
    '& .MuiFormControlLabel-root': {
      marginRight: 0
    }
  },
  inputField: {
    '& .MuiInputLabel-root': {
      textTransform: 'unset',
      fontWeight: 400,
      fontSize: 14
    }
  },
  eitLabels: {
    fontSize: 14
  },
  upgradeDelayInput: {
    width: 100
  },
  chip: {
    marginLeft: theme.spacing(2)
  },
  subContainer: {
    padding: theme.spacing(3)
  },
  subHeading: {
    fontSize: 14,
    fontWeight: 500
  },
  tab: {
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  updateOptions: {
    '& .MuiFormControlLabel-root': {
      alignItems: 'flex-start'
    }
  }
}));

//consts
export const TOAST_AUTO_DISMISS_INTERVAL = 3000;

//fields
export const EIT_FIELD_NAME = 'enableUniverseEncryption';
//cert fields
export const NODE_NODE_CERT_FIELD_NAME = 'rootCA';
export const CLIENT_NODE_CERT_FIELD_NAME = 'clientRootCA';
//toggles
export const ENABLE_NODE_NODE_ENCRYPTION_NAME = 'enableNodeToNodeEncrypt';
export const ENABLE_CLIENT_NODE_ENCRYPTION_NAME = 'enableClientToNodeEncrypt';
export const K8S_ENCRYPTION_TYPE_FIELD = 'k8sEncryptionType';
//other
export const USE_SAME_CERTS_FIELD_NAME = 'rootAndClientRootCASame';
export const USE_ROLLING_UPGRADE_FIELD_NAME = 'rollingUpgrade';
export const ROLLING_UPGRADE_DELAY_FIELD_NAME = 'upgradeDelay';
export const ROLLING_UPGRADE_OPTION_FIELD_NAME = 'upgradeOption';
//rotatecerts
export const ROTATE_NODE_NODE_CERT_FIELD_NAME = 'selfSignedServerCertRotate';
export const ROTATE_CLIENT_NODE_CERT_FIELD_NAME = 'selfSignedClientCertRotate';

// dtos

export enum UpgradeOptions {
  Rolling = 'Rolling',
  NonRolling = 'Non-Rolling',
  NonRestart = 'Non-Restart'
}

export enum K8sEncryptionOption {
  ClienToNode = 'enableClientToNodeEncrypt',
  NodeToNode = 'enableNodeToNodeEncrypt',
  EnableBoth = 'EnableBoth'
}

export interface EncryptionInTransitFormValues {
  enableUniverseEncryption: boolean;
  rootCA?: string | null;
  clientRootCA?: string | null;
  enableNodeToNodeEncrypt: boolean;
  enableClientToNodeEncrypt: boolean;
  rootAndClientRootCASame: boolean;
  rollingUpgrade: boolean;
  upgradeDelay: number;
  selfSignedServerCertRotate?: boolean;
  selfSignedClientCertRotate?: boolean;
  upgradeOption?: string;
  sleepAfterMasterRestartMillis?: number;
  sleepAfterTServerRestartMillis?: number;
  k8sEncryptionType?: K8sEncryptionOption;
}

export enum CertTypes {
  'rootCA' = 'rootCA',
  'clientRootCA' = 'clientRootCA'
}

export const FORM_RESET_VALUES = {
  enableClientToNodeEncrypt: false,
  enableNodeToNodeEncrypt: false,
  rootCA: null,
  clientRootCA: null,
  rootAndClientRootCASame: false
};

export const getInitialFormValues = (
  universeDetails: UniverseDetails,
  isItKubernetesUniverse: boolean
) => {
  const cluster = getPrimaryCluster(universeDetails.clusters);
  const isRootClientCASameforK8s =
    cluster?.userIntent?.enableNodeToNodeEncrypt &&
    cluster?.userIntent.enableClientToNodeEncrypt &&
    universeDetails.rootCA === universeDetails.clientRootCA
      ? true
      : false;
  return {
    enableUniverseEncryption: !!(
      cluster?.userIntent?.enableNodeToNodeEncrypt || cluster?.userIntent.enableClientToNodeEncrypt
    ),
    enableNodeToNodeEncrypt: !!cluster?.userIntent.enableNodeToNodeEncrypt,
    enableClientToNodeEncrypt: !!cluster?.userIntent.enableClientToNodeEncrypt,
    rootCA: universeDetails?.rootCA ?? null,
    clientRootCA: universeDetails?.clientRootCA
      ? universeDetails.clientRootCA
      : universeDetails?.rootAndClientRootCASame
      ? universeDetails.rootCA
      : null,
    rootAndClientRootCASame: isItKubernetesUniverse
      ? isRootClientCASameforK8s
      : !!universeDetails?.rootAndClientRootCASame,
    rollingUpgrade: true,
    upgradeDelay: 240,
    upgradeOption: UpgradeOptions.NonRestart,
    ...(isItKubernetesUniverse &&
      (cluster?.userIntent?.enableNodeToNodeEncrypt ||
        cluster?.userIntent.enableClientToNodeEncrypt) && {
        k8sEncryptionType:
          !!cluster?.userIntent.enableNodeToNodeEncrypt &&
          !!cluster?.userIntent.enableClientToNodeEncrypt
            ? K8sEncryptionOption.EnableBoth
            : cluster?.userIntent.enableClientToNodeEncrypt
            ? K8sEncryptionOption.ClienToNode
            : K8sEncryptionOption.NodeToNode
      })
  };
};

const getCertificateType = (certificate: Certificate) => certificate.certType;

export const isSelfSignedCert = (certificate: Certificate) =>
  getCertificateType(certificate) === 'SelfSigned';

export const isCertManagerCert = (certificate: Certificate) =>
  getCertificateType(certificate) === 'K8SCertManager';

import { Label } from 'react-bootstrap';
import './AlertUtils.scss';

const SEVERITY_LABEL_MAP = {
  SEVERE: 'danger',
  RESOLVED: 'success',
  WARNING: 'warning'
};

export function getSeverityLabel(severity: string, customText?: string) {
  const labelType = SEVERITY_LABEL_MAP[severity] ?? SEVERITY_LABEL_MAP['WARNING'];
  return (
    <Label className={`alert-severity ${labelType}`} bsStyle={labelType}>
      {customText ?? severity}
    </Label>
  );
}

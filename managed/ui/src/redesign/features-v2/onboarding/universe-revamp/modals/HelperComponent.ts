import { mui } from '@yugabyte-ui-library/core';

const { Box, Typography, styled } = mui;

export const DEFAULT_RELEASE_NOTES_URL =
  'https://docs.yugabyte.com/preview/releases/yba-releases/';

export const ModalBody = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  backgroundColor: '#FBFCFD',
  // Figma content: pt 16 / px 24 / pb 24
  padding: '16px 24px 24px',
  boxSizing: 'border-box'
}));

export const SectionCard = styled(Box)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '32px',
  width: '100%',
  padding: '24px 24px 32px',
  backgroundColor: theme.palette.common.white,
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px'
}));

export const GradientTitle = styled(Typography)(() => ({
  fontFamily: 'Inter',
  fontSize: '15px',
  fontWeight: 600,
  lineHeight: '16px',
  backgroundImage:
    'linear-gradient(-69deg, #ED35EC 5.14%, #ED35C5 38.93%, #7879F1 75.17%, #5E60F0 98.9%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  color: 'transparent'
}));

export const SectionTitle = styled(Typography)(() => ({
  fontFamily: 'Inter',
  fontSize: '15px',
  fontWeight: 600,
  lineHeight: '16px',
  color: '#735AF5'
}));

export const FeatureList = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '24px',
  width: '100%'
}));

export const FeatureRow = styled(Box)(() => ({
  display: 'flex',
  gap: '16px',
  alignItems: 'flex-start',
  width: '100%',
  opacity: 0.8
}));

export const BulletWrap = styled(Box)(() => ({
  width: '24px',
  height: '24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: 0,
  '& > svg': {
    width: '8px',
    height: '8px',
    display: 'block'
  }
}));

export const BoltWrap = styled(Box)(() => ({
  width: '24px',
  height: '24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: 0,
  '& > svg': {
    width: '16px',
    height: '20px',
    display: 'block'
  }
}));

export const MapWrap = styled(Box)(() => ({
  width: '24px',
  height: '24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: 0,
  overflow: 'hidden',
  '& > svg': {
    width: '21px',
    height: '22px',
    display: 'block'
  }
}));

export const UserGroupWrap = styled(Box)(() => ({
  width: '24px',
  height: '24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: 0,
  overflow: 'hidden',
  '& > svg': {
    width: '24px',
    height: '24px',
    display: 'block'
  }
}));

/** Figma rollout callout (YBM Primary Blue 100 / 300). */
export const RolloutBanner = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '8px',
  width: '100%',
  padding: '16px 24px',
  borderRadius: '8px',
  border: '1px solid #CBDAFF',
  backgroundColor: '#F2F6FF',
  boxSizing: 'border-box'
}));

export const RuntimeConfigTag = styled(Box)(() => ({
  display: 'inline-flex',
  alignItems: 'center',
  padding: '4px 6px',
  borderRadius: '6px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#FFFFFF',
  fontFamily: 'Menlo, Monaco, Consolas, monospace',
  fontSize: '11px',
  fontWeight: 400,
  lineHeight: 'normal',
  color: '#0B1117',
  whiteSpace: 'nowrap'
}));

export const LearnMoreLink = styled('a')(() => ({
  color: '#4E5F6D',
  fontFamily: 'Inter',
  fontSize: '13px',
  fontWeight: 400,
  lineHeight: '16px',
  textDecoration: 'underline',
  textUnderlinePosition: 'from-font',
  textDecorationSkipInk: 'none',
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'none'
  }
}));

export const RelocationTable = styled(Box)(({ theme }) => ({
  width: '100%',
  border: `1px solid ${theme.palette.grey[200]}`,
  borderRadius: '8px',
  overflow: 'hidden',
  backgroundColor: theme.palette.common.white
}));

export const TableHeader = styled(Box)(({ theme }) => ({
  display: 'grid',
  gridTemplateColumns: 'minmax(0, 1fr) 24px minmax(0, 1.4fr)',
  columnGap: '40px',
  alignItems: 'center',
  minHeight: '34px',
  padding: '0 16px',
  borderBottom: `1px solid ${theme.palette.grey[200]}`
}));

export const TableRow = styled(Box)(({ theme }) => ({
  display: 'grid',
  gridTemplateColumns: 'minmax(0, 1fr) 24px minmax(0, 1.4fr)',
  columnGap: '40px',
  alignItems: 'center',
  minHeight: '32px',
  padding: '0 16px',
  borderBottom: `1px solid ${theme.palette.grey[200]}`,
  '&:last-child': {
    borderBottom: 'none'
  }
}));

export const HeaderLabel = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: '4px',
  fontFamily: 'Inter',
  fontSize: '11.5px',
  fontWeight: 600,
  lineHeight: '16px',
  color: '#0B1117',
  '& > svg': {
    width: '5px',
    height: '9px',
    display: 'block'
  }
}));

export const ArrowWrap = styled(Box)(() => ({
  width: '24px',
  height: '18px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  '& > svg': {
    width: '11.5px',
    height: '11.5px',
    display: 'block',
    transform: 'rotate(180deg)'
  }
}));

export const FooterActions = styled(Box)(({ theme }) => ({
  display: 'flex',
  // Bleed past MUI DialogActions default padding for edge-to-edge footer.
  width: 'calc(100% + 32px)',
  margin: '-16px',
  justifyContent: 'flex-end',
  gap: '8px',
  padding: '16px',
  backgroundColor: theme.palette.grey[100],
  borderTop: `1px solid ${theme.palette.grey[200]}`
}));

export const OnboardingModalContent = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  backgroundColor: '#FFFFFF',
  padding: '24px'
}));

export const HeroCard = styled(Box)(() => ({
  position: 'relative',
  width: '100%',
  height: '300px',
  overflow: 'hidden',
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  background:
    'radial-gradient(ellipse at 47% 100%, rgba(242, 243, 254, 1) 0%, rgba(255, 255, 255, 1) 70%)'
}));

export const CalloutPill = styled(Box)(() => ({
  display: 'inline-flex',
  alignItems: 'center',
  justifyContent: 'center',
  padding: '6px 10px',
  borderRadius: '50px',
  backgroundColor: '#E8E9FE',
  color: '#735AF5',
  fontSize: '11.5px',
  fontWeight: 600,
  lineHeight: '20px',
  whiteSpace: 'nowrap'
}));

export const InfoTipBox = styled(Box)(() => ({
  display: 'flex',
  gap: '8px',
  alignItems: 'flex-start',
  width: '100%',
  padding: '16px',
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#F7F9FB'
}));

export const TipIconWrap = styled(Box)(() => ({
  width: '24px',
  height: '24px',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flexShrink: 0,
  overflow: 'hidden',
  '& > svg': {
    width: '21px',
    height: '22px',
    display: 'block'
  }
}));

export const NoteCard = styled(Box)(() => ({
  display: 'flex',
  flexDirection: 'column',
  gap: '8px',
  width: '100%',
  padding: '16px',
  borderRadius: '8px',
  border: '1px solid #D7DEE4',
  backgroundColor: '#FBFCFD'
}));

export const PathMappingRow = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: '16px',
  width: '100%'
}));

export const PathMappingLabel = styled(Box)(() => ({
  display: 'flex',
  alignItems: 'center',
  gap: '4px',
  flexShrink: 0
}));

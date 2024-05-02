import { CONST_VALUES, verifyAttributes } from './UniverseUtils';

const VALID_JWT_GFLAG_INPUT = {
  VALID:
    'host all all 0.0.0.0/0 jwt map="map1" jwt_audiences=""f1ce4fa4-05ef-4899-9ca3-29166626e78b"" jwt_issuers=""https://login.microsoftonline.com/810c029b-d266-4f13-a23a-54b66cfb5f83/v2.0"" jwt_matching_claim_key=""preferred_username""',
  CHANGE_FIRST_INSTANCE:
    'host all all +jwt_service_users 0.0.0.0/0 jwt map="map1" jwt_audiences=""f1ce4fa4-05ef-4899-9ca3-29166626e78b"" jwt_issuers=""https://login.microsoftonline.com/810c029b-d266-4f13-a23a-54b66cfb5f83/v2.0"" jwt_matching_claim_key=""preferred_username""'
};

const INVALID_JWT_GFLAG_INPUT = {
  WRONG_ATTR:
    'host all all 0.0.0.0/0 jwt map="map1" jwt_audien=""f1ce4fa4-05ef-4899-9ca3-29166626e78b"" jwt_issuers=""https://login.microsoftonline.com/810c029b-d266-4f13-a23a-54b66cfb5f83/v2.0"" jwt_matching_claim_key=""preferred_username""',
  MISSING_EQUALS:
    'host all all 0.0.0.0/0 jwt map="map1" jwt_audiences""f1ce4fa4-05ef-4899-9ca3-29166626e78b"" jwt_issuers=""https://login.microsoftonline.com/810c029b-d266-4f13-a23a-54b66cfb5f83/v2.0"" jwt_matching_claim_key=""preferred_username""',
  MISSING_QUOTES:
    'host all all 0.0.0.0/0 jwt map="map1" jwt_audiences=""f1ce4fa4-05ef-4899-9ca3-29166626e78b"" jwt_issuers=""https://login.microsoftonline.com/810c029b-d266-4f13-a23a-54b66cfb5f83/v2.0"" jwt_matching_claim_key=""preferred_username'
};

const VALID_LDAP_GFLAG_INPUT = {
  VALID:
    'host all 10.0.0.0/8 ldap ldapurl=ldaps://ldap.dev.schwab.com:636 ldapsearchattribute=""sAMAccountName"" ldapbasedn=""OU=ServiceAccount,DC=csdev,DC=corp"" ldapbinddn=""CN=svc.yb_ldap_dev,OU=ServiceAccount,DC=csdev,DC=corp"" ldapbindpasswd=""Password""',
  CHANGE_FIRST_INSTANCE:
    'host all +ldap_service_users 10.0.0.0/8 ldap ldapurl=ldaps://ldap.dev.schwab.com:636 ldapsearchattribute=""sAMAccountName"" ldapbasedn=""OU=ServiceAccount,DC=csdev,DC=corp"" ldapbinddn=""CN=svc.yb_ldap_dev,OU=ServiceAccount,DC=csdev,DC=corp"" ldapbindpasswd=""Password""'
};

const INVALID_LDAP_GFLAG_INPUT = {
  WRONG_ATTR:
    'host all 10.0.0.0/8 ldap ldapurl=ldaps://ldap.dev.schwab.com:636 ldapsearchattribu=""sAMAccountName"" ldapbasedn=""OU=ServiceAccount,DC=csdev,DC=corp"" ldapbinddn=""CN=svc.yb_ldap_dev,OU=ServiceAccount,DC=csdev,DC=corp"" ldapbindpasswd=""Password""',
  MISSING_EQUALS:
    'host all 10.0.0.0/8 ldap ldapurl=ldaps://ldap.dev.schwab.com:636 ldapsearchattribute=""sAMAccountName"" ldapbasedn""OU=ServiceAccount,DC=csdev,DC=corp"" ldapbinddn=""CN=svc.yb_ldap_dev,OU=ServiceAccount,DC=csdev,DC=corp"" ldapbindpasswd=""Password""',
  MISSING_QUOTES:
    'host all 10.0.0.0/8 ldap ldapurl=ldaps://ldap.dev.schwab.com:636 ldapsearchattribute=""sAMAccountName"" ldapbasedn=""OU=ServiceAccount,DC=csdev,DC=corp"" ldapbinddn=""CN=svc.yb_ldap_dev,OU=ServiceAccount,DC=csdev,DC=corp"" ldapbindpasswd=""Password'
};

describe('Verify Attributes for JWT values', () => {
  it('Unsupported OIDC should return error', () => {
    expect(verifyAttributes('', CONST_VALUES.JWT, null, false)).toEqual({
      errorMessageKey: 'universeForm.gFlags.jwksNotSupported',
      isAttributeInvalid: true,
      isWarning: false
    });
  });

  it('Invalid JWSKeyset should return error', () => {
    expect(verifyAttributes('', CONST_VALUES.JWT, null, true)).toEqual({
      errorMessageKey: 'universeForm.gFlags.uploadKeyset',
      isAttributeInvalid: true,
      isWarning: false
    });
  });

  it('Empty GFlag input should not return error', () => {
    expect(verifyAttributes('', CONST_VALUES.JWT, '123', true)).toEqual({
      errorMessageKey: '',
      isAttributeInvalid: false,
      isWarning: false
    });
  });

  it('Valid GFlag input should not return error', () => {
    expect(verifyAttributes(VALID_JWT_GFLAG_INPUT.VALID, CONST_VALUES.JWT, '123', true)).toEqual({
      errorMessageKey: '',
      isAttributeInvalid: false,
      isWarning: false
    });
  });

  it('Valid GFlag input with first instance of JWT keyword modified should not return error', () => {
    expect(
      verifyAttributes(VALID_JWT_GFLAG_INPUT.CHANGE_FIRST_INSTANCE, CONST_VALUES.JWT, '123', true)
    ).toEqual({
      errorMessageKey: '',
      isAttributeInvalid: false,
      isWarning: false
    });
  });

  it('Wrong JWT Attribute GFlag input should return error', () => {
    expect(
      verifyAttributes(INVALID_JWT_GFLAG_INPUT.WRONG_ATTR, CONST_VALUES.JWT, '123', true)
    ).toEqual({
      errorMessageKey: 'universeForm.gFlags.invalidKey',
      isAttributeInvalid: true,
      isWarning: false
    });
  });

  it('Missing equals sign in JWT Attribute GFlag input should return error', () => {
    expect(
      verifyAttributes(INVALID_JWT_GFLAG_INPUT.MISSING_EQUALS, CONST_VALUES.JWT, '123', true)
    ).toEqual({
      errorMessageKey: 'universeForm.gFlags.invalidKey',
      isAttributeInvalid: true,
      isWarning: false
    });
  });

  it('Missing double quotes in JWT attribute GFlag input should return error', () => {
    expect(
      verifyAttributes(INVALID_JWT_GFLAG_INPUT.MISSING_QUOTES, CONST_VALUES.JWT, '123', true)
    ).toEqual({
      errorMessageKey: 'universeForm.gFlags.missingQuoteAttributeValue',
      isAttributeInvalid: true,
      isWarning: false
    });
  });
});

describe('Verify Attributes for LDAP values', () => {
  it('Empty GFlag input should not return error', () => {
    expect(verifyAttributes('', CONST_VALUES.LDAP, '123', true)).toEqual({
      errorMessageKey: '',
      isAttributeInvalid: false,
      isWarning: false
    });
  });

  it('Valid GFlag input should not return error', () => {
    expect(verifyAttributes(VALID_LDAP_GFLAG_INPUT.VALID, CONST_VALUES.LDAP, null, false)).toEqual({
      errorMessageKey: '',
      isAttributeInvalid: false,
      isWarning: false
    });
  });

  it('Valid GFlag input with first instance of LDAP keyword modified should not return error', () => {
    expect(
      verifyAttributes(VALID_LDAP_GFLAG_INPUT.CHANGE_FIRST_INSTANCE, CONST_VALUES.LDAP, null, false)
    ).toEqual({
      errorMessageKey: '',
      isAttributeInvalid: false,
      isWarning: false
    });
  });

  it('Wrong LDAP attribute GFlag input should return error', () => {
    expect(
      verifyAttributes(INVALID_LDAP_GFLAG_INPUT.WRONG_ATTR, CONST_VALUES.LDAP, null, false)
    ).toEqual({
      errorMessageKey: 'universeForm.gFlags.invalidKey',
      isAttributeInvalid: true,
      isWarning: false
    });
  });

  it('Missing equals sign in LDAP attribute GFlag input should return error', () => {
    expect(
      verifyAttributes(INVALID_LDAP_GFLAG_INPUT.MISSING_EQUALS, CONST_VALUES.LDAP, null, false)
    ).toEqual({
      errorMessageKey: 'universeForm.gFlags.invalidKey',
      isAttributeInvalid: true,
      isWarning: false
    });
  });

  it('Missing double quotes in LDAP attribute GFlag input should return error', () => {
    expect(
      verifyAttributes(INVALID_LDAP_GFLAG_INPUT.MISSING_QUOTES, CONST_VALUES.LDAP, null, false)
    ).toEqual({
      errorMessageKey: 'universeForm.gFlags.missingQuoteAttributeValue',
      isAttributeInvalid: true,
      isWarning: false
    });
  });
});

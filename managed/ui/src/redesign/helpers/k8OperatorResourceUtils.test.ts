// Copyright (c) YugabyteDB, Inc.

import { RuntimeConfigKey } from './constants';
import { isK8OperatorApiBlocked } from './k8OperatorResourceUtils';

describe('isK8OperatorApiBlocked', () => {
  const blockFlagOn = [{ key: RuntimeConfigKey.BLOCK_K8_OPERATOR, value: 'true' }];
  const blockFlagOff = [{ key: RuntimeConfigKey.BLOCK_K8_OPERATOR, value: 'false' }];

  it('is true only when the universe is operator-controlled and the runtime flag is on', () => {
    expect(isK8OperatorApiBlocked(true, blockFlagOn)).toBe(true);
  });

  it('is false when the runtime flag is off', () => {
    expect(isK8OperatorApiBlocked(true, blockFlagOff)).toBe(false);
  });

  it('is false when the universe is not operator-controlled', () => {
    expect(isK8OperatorApiBlocked(false, blockFlagOn)).toBe(false);
  });

  it('is false when config entries are missing', () => {
    expect(isK8OperatorApiBlocked(true, null)).toBe(false);
    expect(isK8OperatorApiBlocked(true, undefined)).toBe(false);
  });
});

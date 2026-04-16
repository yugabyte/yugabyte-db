//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "pgrx-cshim-static.c"

void SpinLockInit__pgrx_cshim(volatile slock_t *lock) {
    SpinLockInit(lock);
}

void SpinLockAcquire__pgrx_cshim(volatile slock_t *lock) {
    SpinLockAcquire(lock);
}

void SpinLockRelease__pgrx_cshim(volatile slock_t *lock) {
    SpinLockRelease(lock);
}

bool SpinLockFree__pgrx_cshim(slock_t *lock) {
    return SpinLockFree(lock);
}

int call_closure_with_sigsetjmp(int savemask, void* closure_env_ptr, int (*closure_code)(sigjmp_buf jbuf, void *env_ptr)) {
    sigjmp_buf jbuf;
    int val;
    if (0 == (val = sigsetjmp(jbuf, savemask))) {
        return closure_code(jbuf, closure_env_ptr);
    } else {
        return val;
    }
}

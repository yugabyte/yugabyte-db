// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_CRYPT_H_
#define YB_UTIL_CRYPT_H_

#include <stdint.h>

namespace yb {
namespace util {

static constexpr uint16_t kBcryptHashSize = 64;

// Given a workfactor to slow down hash generation, create a salt.
// workfactor: the exponentiation workfactor
// hash: the output salt
// return 0 on success -1 on failure
int bcrypt_gensalt(int workfactor, char salt[kBcryptHashSize]);

// Given a password and a pre-generated salt, hash the password.
// passwd :the password
// salt: the salt
// hash: the hash to be generated
// return 0 on success -1 on failure
int bcrypt_hashpw(
    const char* passwd, const char salt[kBcryptHashSize], char hash[kBcryptHashSize]);

// Given a password, generate a random salt and use it to salt and hash the password.
// passwd: the given password
// hash: the hash to be generated
// return 0 on success -1 on failure
int bcrypt_hashpw(const char* passwd, char hash[kBcryptHashSize]);

// Check if the provided password matches the hash.
// passwd: the password
// hash: the salted hash to check against
// return 0 on success -1 on failure
int bcrypt_checkpw(const char* passwd, const char hash[kBcryptHashSize]);

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_CRYPT_H_

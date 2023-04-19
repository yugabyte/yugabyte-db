# - Try to find boringssl include dirs and libraries
#
# Usage of this module as follows:
#
#     find_package(BORINGSSL)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  BORINGSSL_ROOT_DIR          Set this variable to the root installation of
#                            boringssl if the module has problems finding the
#                            proper installation path.
#
# Variables defined by this module:
#
#  BORINGSSL_FOUND             System has boringssl, include and library dirs found
#  BORINGSSL_INCLUDE_DIR       The boringssl include directories.
#  BORINGSSL_LIBRARIES         The boringssl libraries.
#  BORINGSSL_CRYPTO_LIBRARY    The boringssl crypto library.
#  BORINGSSL_SSL_LIBRARY       The boringssl ssl library.

find_path(BORINGSSL_ROOT_DIR
          NAMES include/openssl/ssl.h include/openssl/base.h include/openssl/hkdf.h
          HINTS ${BORINGSSL_ROOT_DIR})

find_path(BORINGSSL_INCLUDE_DIR
          NAMES openssl/ssl.h openssl/base.h openssl/hkdf.h
          HINTS ${BORINGSSL_ROOT_DIR}/include)

find_library(BORINGSSL_SSL_LIBRARY
            NAMES libssl.a
            HINTS ${BORINGSSL_ROOT_DIR}/build/ssl)

find_library(BORINGSSL_CRYPTO_LIBRARY
             NAMES libcrypto.a
             HINTS ${BORINGSSL_ROOT_DIR}/build/crypto)

set(BORINGSSL_LIBRARIES ${BORINGSSL_SSL_LIBRARY} ${BORINGSSL_CRYPTO_LIBRARY}
    CACHE STRING "BoringSSL SSL and crypto libraries" FORCE)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(BORINGSSL DEFAULT_MSG
                                  BORINGSSL_LIBRARIES
                                  BORINGSSL_INCLUDE_DIR)

mark_as_advanced(
        BORINGSSL_ROOT_DIR
        BORINGSSL_INCLUDE_DIR
        BORINGSSL_LIBRARIES
        BORINGSSL_CRYPTO_LIBRARY
        BORINGSSL_SSL_LIBRARY
)

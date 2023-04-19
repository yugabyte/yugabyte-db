
# - Try to find the LDAP libraries
#
#  LDAP_FOUND - ldap was successfully found
#  LDAP_INCLUDE_DIR - ldap include directory
#  LDAP_LIBRARIES - ldap libraries

find_path(LDAP_INCLUDE_DIR NAMES ldap.h)
find_library(LDAP_LIBRARY ldap)

find_package_handle_standard_args(LDAP REQUIRED_VARS LDAP_LIBRARY LDAP_INCLUDE_DIR)

import hashlib

# When OpenSSL has only the FIPS provider, hashlib has no md5 at all, and modules that import it by
# name - paramiko among them - fail to import. Give them one that fails only when called, the way
# md5 does in FIPS mode otherwise.
if not hasattr(hashlib, "md5"):
    def _md5_unavailable(*args, **kwargs):
        raise ValueError("unsupported hash type md5 (in FIPS mode)")

    hashlib.md5 = _md5_unavailable

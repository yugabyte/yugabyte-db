#ifndef KIWI_PASSWORD_H
#define KIWI_PASSWORD_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_password kiwi_password_t;

struct kiwi_password {
	char *password;
	int password_len;
};

static inline void kiwi_password_init(kiwi_password_t *pw)
{
	pw->password = NULL;
	pw->password_len = 0;
}

static inline void kiwi_password_copy(kiwi_password_t *dst_pw,
				      const kiwi_password_t *src_pw)
{
	assert(dst_pw->password_len == 0);
	assert(dst_pw->password == NULL);

	dst_pw->password_len = src_pw->password_len;
	dst_pw->password = (char *)malloc(sizeof(char) * src_pw->password_len);
	strncpy(dst_pw->password, src_pw->password, src_pw->password_len);
}

static inline void kiwi_password_free(kiwi_password_t *pw)
{
	if (pw->password)
		free(pw->password);
}

static inline int kiwi_password_compare(kiwi_password_t *a, kiwi_password_t *b)
{
	return (a->password_len == b->password_len) &&
	       (memcmp(a->password, b->password, a->password_len) == 0);
}

static inline uint32_t kiwi_password_salt(kiwi_key_t *key, uint32_t rand)
{
	return rand ^ key->key ^ key->key_pid;
}

__attribute__((hot)) static inline int
kiwi_password_md5(kiwi_password_t *pw, char *user, int user_len, char *password,
		  int password_len, char salt[4])
{
	uint8_t digest_prepare[16];
	char digest_prepare_sz[32];
	uint8_t digest[16];
	kiwi_md5_t ctx;
	if (password_len == 35 && memcmp(password, "md5", 3) == 0) {
		/* digest = md5(digest, salt) */
		kiwi_md5_init(&ctx);
		kiwi_md5_update(&ctx, password + 3, 32);
		kiwi_md5_update(&ctx, salt, 4);
		kiwi_md5_final(&ctx, digest);
	} else {
		/* digest = md5(password, user) */
		kiwi_md5_init(&ctx);
		kiwi_md5_update(&ctx, password, password_len);
		kiwi_md5_update(&ctx, user, user_len);
		kiwi_md5_final(&ctx, digest_prepare);
		kiwi_md5_tostring(digest_prepare_sz, digest_prepare);
		/* digest = md5(digest, salt) */
		kiwi_md5_init(&ctx);
		kiwi_md5_update(&ctx, digest_prepare_sz, 32);
		kiwi_md5_update(&ctx, salt, 4);
		kiwi_md5_final(&ctx, digest);
	}

	/* 'md5' + to_string(digest) */
	pw->password_len = 35 + 1;
	pw->password = malloc(pw->password_len);
	if (pw->password == NULL)
		return -1;
	pw->password[0] = 'm';
	pw->password[1] = 'd';
	pw->password[2] = '5';
	kiwi_md5_tostring((pw->password + 3), digest);
	pw->password[35] = 0;
	return 0;
}

#endif /* KIWI_PASSWORD_H */

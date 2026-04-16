
/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

#include "kiwi.h"

static inline void kiwi_long_option_rewrite(char *name, int name_len)
{
	assert(name);

	for (int i = 0; i < name_len; ++i) {
		if (name[i] == '-') {
			name[i] = '_';
		}
	}
}

int kiwi_parse_options_and_update_vars(kiwi_vars_t *vars, char *pgoptions,
				       int pgoptions_len)
{
	if (pgoptions == NULL) {
		return -1;
	}
	int errs = 0;

	char optarg_buf[KIWI_MAX_VAR_SIZE];
	char optval_buf[KIWI_MAX_VAR_SIZE];
	int len = 0;
	for (int i = 0; i < pgoptions_len; ++i) {
		if (pgoptions[i] == '\0') {
			// faces null inside string, reject all other opts
			break;
		}
		++len;
	}
	pgoptions_len = len;

	for (int i = 0; i < pgoptions_len;) {
		if (isspace(pgoptions[i])) {
			// skip initial spaces
			++i;
			continue;
		}
		// opts are in form --opt=val of -c opt=val, reject all other format
		if (pgoptions[i] != '-' || i + 1 >= pgoptions_len) {
			break;
		}

		++i;
		int j;
		int optarg_pos, optval_pos;
		int optarg_len, optval_len;

		switch (pgoptions[i]) {
		case 'c':
			++i;
			// skip spaces after
			//  -c*spaces**values*
			while (i < pgoptions_len && isspace(pgoptions[i])) {
				++i;
			}
			break;
		case '-':
			++i;

			break;
		default:
			return errs;
		}

		if (i >= pgoptions_len) {
			return errs;
		}
		// now we are looking at *probably* first char of opt name
		j = i;
		while (j < pgoptions_len && pgoptions[j] != '=') {
			++j;
		}
		// equal sign not found
		if (j == pgoptions_len) {
			++errs;
			return errs;
		}

		optarg_pos = i;
		optarg_len = j - i;

		if (optarg_len == 0) {
			// empty opt name
			++errs;
			return errs;
		}
		// skip equal sign
		++j;
		if (j == pgoptions_len || isspace(pgoptions[j])) {
			// case:
			// -c opt=*end*
			// -c opt= *smthg*
			++errs;
			return errs;
		}
		// now we are looking at first char of opt value
		i = j;
		optval_pos = i;
		while (j + 1 < pgoptions_len) {

			// if space found, check if we really are at the end of value
			if (isspace(pgoptions[j + 1])) {
				int k = j + 1;
				while (k + 1 < pgoptions_len && isspace(pgoptions[k + 1])) {
					++k;
				}

				// start of next opt
				if (k + 1 < pgoptions_len && pgoptions[k + 1] == '-') {
					break;
				}

				j = k;
				continue;
			}
			++j;
		}
		optval_len = j - i + 1;

		kiwi_long_option_rewrite(pgoptions + optarg_pos, optarg_len);

		if (optarg_len + 1 >= KIWI_MAX_VAR_SIZE ||
		    optval_len + 1 >= KIWI_MAX_VAR_SIZE) {
			break;
		}

		memcpy(optarg_buf, pgoptions + optarg_pos, optarg_len);
		optarg_buf[optarg_len] = 0;

		memcpy(optval_buf, pgoptions + optval_pos, optval_len);
		optval_buf[optval_len] = 0;

		kiwi_vars_update(vars, optarg_buf, optarg_len + 1, optval_buf,
				 optval_len + 1);
		i = j + 1;
	}

	return errs;
}

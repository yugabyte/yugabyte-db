#ifndef ODYSSEY_MISC_H
#define ODYSSEY_MISC_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

extern bool parse_bool(const char *value, bool *result);
extern bool parse_bool_with_len(const char *value, size_t len, bool *result);

#endif /* ODYSSEY_MISC_H */

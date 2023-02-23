/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

__thread unsigned short prng_seed[3];
__thread unsigned short *prng_state = NULL;

long int pg_lrand48(unsigned short *_rand48_seed);
void pg_srand48(long seed, unsigned short *_rand48_seed);

void mm_lrand48_seed(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	long int rand_seed_2 = 0;
	long int rand_seed;
	rand_seed = getpid() ^ getuid() ^ tv.tv_sec ^ tv.tv_usec;

	int fd;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd == -1)
		fd = open("/dev/random", O_RDONLY);
	if (fd != -1) {
		int rc = read(fd, &rand_seed_2, sizeof(rand_seed_2));
		(void)rc;
		close(fd);
	}

	rand_seed ^= rand_seed_2;
	pg_srand48(rand_seed, prng_seed);
	prng_state = prng_seed;
}

long int machine_lrand48(void)
{
	assert(prng_state);
	return pg_lrand48(prng_state);
}

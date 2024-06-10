
#include "odyssey.h"
#include <odyssey_test.h>

void test_od_hba_reader_prefix(sa_family_t net, char *prefix, char *value)
{
	od_hba_rule_t *hba = NULL;
	char buffer[INET6_ADDRSTRLEN];
	hba = od_hba_rule_create();
	hba->addr.ss_family = net;
	test(od_hba_reader_prefix(hba, prefix) == 0);
	if (net == AF_INET) {
		struct sockaddr_in *addr = (struct sockaddr_in *)&hba->mask;
		inet_ntop(net, &addr->sin_addr, buffer, sizeof(buffer));
	} else {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&hba->mask;
		inet_ntop(net, &addr->sin6_addr, buffer, sizeof(buffer));
	}
	test(memcmp(value, buffer, strlen(buffer)) == 0);
	od_hba_rule_free(hba);
}

void odyssey_test_hba(void)
{
	test_od_hba_reader_prefix(AF_INET, "31", "255.255.255.254");
	test_od_hba_reader_prefix(AF_INET, "24", "255.255.255.0");
	test_od_hba_reader_prefix(AF_INET, "12", "255.240.0.0");
	test_od_hba_reader_prefix(AF_INET, "7", "254.0.0.0");
	test_od_hba_reader_prefix(AF_INET6, "10", "ffc0::");
	test_od_hba_reader_prefix(AF_INET6, "120",
				  "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ff00");
}
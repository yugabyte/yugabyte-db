#ifndef ODYSSEY_HBA_READER_H
#define ODYSSEY_HBA_READER_H

int od_hba_reader_parse(od_config_reader_t *reader);
int od_hba_reader_prefix(od_hba_rule_t *hba, char *prefix);

#endif /* ODYSSEY_HBA_READER_H */

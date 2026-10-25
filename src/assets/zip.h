/*
 * Minimal zip reader for the user's ROM set (starforc.zip): central
 * directory, stored and deflated entries, CRC-32 checked.
 */
#ifndef STARFORCE_ZIP_H
#define STARFORCE_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* Read the entry whose base name matches `name` (case-insensitive) into a
   malloc'd buffer. Returns 0; -1 if the zip can't be read (message in err),
   -2 if there is no such entry. */
int zip_read(const char *zip_path, const char *name, uint8_t **data, size_t *len, char *err, int errlen);
/* The same for the entry whose CRC-32 is `crc`, whatever its name. */
int zip_read_crc(const char *zip_path, uint32_t crc, uint8_t **data, size_t *len, char *err, int errlen);

#endif

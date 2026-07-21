#ifndef _STUB_UUID_H
#define _STUB_UUID_H
typedef unsigned char uuid_t[16];
void uuid_generate_random(uuid_t out);
void uuid_unparse_upper(const uuid_t uu, char *out);
#endif

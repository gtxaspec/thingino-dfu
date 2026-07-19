#include <uuid/uuid.h>
#include <stdio.h>
#include <sys/random.h>
void uuid_generate_random(uuid_t out) {
    if (getentropy(out, 16) != 0) { for (int i=0;i<16;i++) out[i]=(unsigned char)(i*37+1); }
    out[6] = (out[6] & 0x0f) | 0x40;   /* v4 */
    out[8] = (out[8] & 0x3f) | 0x80;   /* variant */
}
void uuid_unparse_upper(const uuid_t u, char *o) {
    sprintf(o, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],u[11],u[12],u[13],u[14],u[15]);
}

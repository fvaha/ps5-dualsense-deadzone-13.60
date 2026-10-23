#ifndef DZ_HTTP_H
#define DZ_HTTP_H
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct request { char method[8], path[80], host[80], origin[100]; size_t length; int local_header; };
static inline int parse_request(char *header, struct request *out) {
    memset(out, 0, sizeof(*out));
    char *line = strstr(header, "\r\n");
    if (!line) return -1;
    *line = 0;
    char *first = strchr(header, ' '), *second = first ? strchr(first + 1, ' ') : NULL;
    if (!first || !second || first == header || second == first + 1 ||
        (size_t)(first - header) >= sizeof(out->method) ||
        (size_t)(second - first - 1) >= sizeof(out->path) || strcmp(second + 1, "HTTP/1.1")) return -1;
    memcpy(out->method, header, first - header);
    memcpy(out->path, first + 1, second - first - 1);
    int lengths = 0, hosts = 0, origins = 0, commands = 0;
    for (char *p = line + 2; *p;) {
        char *end = strstr(p, "\r\n");
        if (!end) return -1;
        *end = 0;
        if (!*p) break;
        char *colon = strchr(p, ':');
        if (!colon || p[0] == ' ' || p[0] == '\t') return -1;
        *colon++ = 0;
        if (!*p) return -1;
        for (const char *c = p; *c; ++c)
            if (!isalnum((unsigned char)*c) && !strchr("!#$%&'*+-.^_`|~", *c)) return -1;
        if (!strcasecmp(p, "Host")) {
            while (*colon == ' ' || *colon == '\t') ++colon;
            if (++hosts != 1 || !*colon || strlen(colon) >= sizeof(out->host)) return -1;
            strcpy(out->host, colon);
        }
        if (!strcasecmp(p, "Origin")) {
            while (*colon == ' ' || *colon == '\t') ++colon;
            if (++origins != 1 || !*colon || strlen(colon) >= sizeof(out->origin)) return -1;
            strcpy(out->origin, colon);
        }
        while (*colon == ' ' || *colon == '\t') ++colon;
        if (!strcasecmp(p, "Transfer-Encoding")) return -1;
        if (!strcasecmp(p, "Content-Length")) {
            if (++lengths != 1 || !*colon) return -1;
            for (char *c = colon; *c; ++c) if (!isdigit((unsigned char)*c)) return -1;
            unsigned long length = strtoul(colon, NULL, 10);
            if (length > 80) return -1;
            out->length = length;
        }
        if (!strcasecmp(p, "X-Deadzone-Request")) {
            if (++commands != 1) return -1;
            out->local_header = !strcmp(colon, "1");
        }
        p = end + 2;
    }
    return hosts == 1 ? 0 : -1;
}
#endif

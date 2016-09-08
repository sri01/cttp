
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "http_header_parse.h"
#include "cref/ref.h"
#include "cref/types.h"
#include "cref/list.h"

#define CONTINUE_IF(fn) \
    do { \
        if (!(fn)) { \
            goto fail; \
        } \
    } while(0)

#define REPEAT(n) for(size_t ___ITR=0;___ITR<(n);___ITR++)

#ifdef DEBUG
#define LOGALL(str) printf("%s", str->buf)
#else
#define LOGALL(str) (void)(str)
#endif

void set_stream_force_read_size(stream_t *stream, size_t i) {
    stream->force_read_size = i;
}

bool _get_next_char(stream_t *stream, char *write) {
    if (stream->bufsz == 0) {
        return false;
    }
    if (stream->bufsz > BUF_MAX) {
        return false;
    }
    if (stream->bufindex < stream->bufsz) {
        *write = (stream->buf)[stream->bufindex++];
        return true;
    }
    if (stream->bufindex == stream->bufsz) {
        *write = (stream->buf)[stream->bufindex];
        stream->bufindex = 0;
        if (stream->bufsz == BUF_MAX) {
            memset(stream->buf, 0, BUF_MAX);
            stream->bufsz = read(stream->fd_read, stream->buf, BUF_MAX);
            stream->read += stream->bufsz;
            LOGALL(stream);
            return true;
        }
        stream->bufsz = 0;
        return true;
    }
}

bool peek_char(stream_t *stream, char *write) {
    if (stream->done) {
        return false;
    }
    *write = stream->next;
    return true;
}

bool pop_char(stream_t *stream, char *write) {
    if (stream->done) {
        return false;
    }
    *write = stream->next;

    if (!_get_next_char(stream, &stream->next)) {
        if (stream->force_read_size > stream->read) {
            usleep(10);
            memset(stream->buf, 0, BUF_MAX);
            stream->bufsz = read(stream->fd_read, stream->buf, BUF_MAX);
            stream->read += stream->bufsz;
            LOGALL(stream);
            pop_char(stream, write);
        } else {
            stream->done = true;
        }
    }
    return true;
}

stream_t *init_stream(int fd) {
    stream_t *result = calloc(1, sizeof(stream_t));
    result->fd_read = fd;
    result->bufindex = 0;
    result->bufsz = read(fd, result->buf, BUF_MAX);
    result->read = result->bufsz;
    LOGALL(result);
    char bogus;
    pop_char(result, &bogus);
    return result;
}

bool is_space(char c) {
    return c==' '
        || c=='\t'
        || c==0
        || c=='\r'
        || c=='\n';
}

bool skip_spaces(stream_t *stream, size_t max) {
    char test;
    while(max--) {
        if(peek_char(stream, &test)) { if (is_space(test)) {
                pop_char(stream, &test); // if space, pop to next
            } else {
                return true;
            }
        } else {
            return false;
        }
    }
}

bool read_until_space(char **location, stream_t *stream, size_t max) {
    bool alloc = *location==NULL; // we are responsible for allocation or not
    size_t index = 0;
    size_t strsize = 10;
    if (alloc) {
        *location = calloc(strsize, sizeof(char));
    }
    while(max--) {
        char tmp;
        if (!peek_char(stream, &tmp)) {
            return index > 0;
        }
        if (is_space(tmp)) {
            return true;
        }
        (*location)[index] = tmp;
        pop_char(stream, &tmp);
        index++;
        if (index+1 == strsize) {
            strsize *=2;
            *location = realloc(*location, strsize*sizeof(char));
        }
    }
    return true;
}

size_t read_until_CRLF(char **location, stream_t *stream, size_t max) {
    bool alloc = *location==NULL; // we are responsible for allocation or not
    bool crlf = false;
    char junk;
    size_t index = 0;
    size_t strsize = 10;
    if (alloc) {
        *location = calloc(strsize, sizeof(char));
    }
    while(max--) {
        char tmp;
        if (!peek_char(stream, &tmp)) {
            return index;
        }
        if (tmp=='\n' && crlf) {
            (*location)[index-1] = 0;
            pop_char(stream, &junk);
            return index;
        }
        if (tmp=='\r') {
            crlf=true;
        } else {
            crlf=false;
        }
        (*location)[index] = tmp;
        pop_char(stream, &junk);
        index++;
        if (index+1 == strsize) {
            strsize *=2;
            *location = realloc(*location, strsize*sizeof(char));
        }
    }
    return index;
}

size_t read_len(char **location, stream_t *stream, size_t max) {
    bool alloc = *location==NULL; // we are responsible for allocation or not
    size_t index = 0;
    size_t strsize = 10;
    if (alloc) {
        *location = calloc(strsize, sizeof(char));
    }
    while(max--) {
        char tmp;
        if (!peek_char(stream, &tmp)) {
            return index;
        }
        (*location)[index] = tmp;
        pop_char(stream, &tmp);
        index++;
        if (index+1 == strsize) {
            strsize *=2;
            *location = realloc(*location, strsize*sizeof(char));
        }
    }
    return index;
}

size_t read_to_null(stream_t *stream) {
    char c;
    size_t ct = 0;
    while(1) {
        if (!pop_char(stream, &c)) {
            return ct;
        }
        ct++;
    }
}

size_t read_excluding(char **buf, stream_t *src, char *pat, size_t max, bool bin) {
    *buf = calloc(sizeof(char), max);
    size_t pat_i = 0;
    size_t buf_i = 0;
    char n = 0;
    while(pop_char(src, &n)) {
        if (n != 0 || bin) {
            if (pat[pat_i] == 0) {
                return buf_i;
            } else if (pat[pat_i] == n) {
                pat_i++;
            } else if (pat_i != 0) {
                for(size_t i=0; i<pat_i; i++) {
                    (*buf)[buf_i] = pat[i];
                    buf_i++;
                }
                pat_i = 0;
            } else {
                (*buf)[buf_i] = n;
                buf_i++;
            }
        }
    }
    return buf_i;
}

header_kv *_header_kv(string *key, string *value) {
    header_kv *result = ref_malloc(sizeof(header_kv), 2);
    result->key = S(key);
    result->value = S(value);
    result->destructor = NULL;
    result->hashcode = NULL;
    result->equals = NULL;
    return result;
}

string *copy_until(char *str, char c) {
    size_t i=0;
    while(str[i] && str[i] != c) {
        i++;
    }
    char *dest = calloc(i+1, sizeof(char));
    memcpy(dest, str, i);
    return _string(dest, 1);
}

void free_header(header_t *header) {
    if (!header) {
        return;
    }
    if (header->verb) {
        free(header->verb);
    }
    if (header->path) {
        free(header->path);
    }
    if (header->version) {
        free(header->version);
    }
}

header_t *_header_t() {
    header_t *result = ref_malloc(sizeof(header_t), 1);
    result->verb = NULL;
    result->path = NULL;
    result->version = NULL;
    result->header_keys = EMPTY;
    result->err_code = 0;
    result->destructor = free_header;
    return result;
}

char *application_match(char *str, const char *match) {
    size_t len = strlen(match);
    if (!strncmp(str, match, len)) {
        return strdup(&(str[len]));
    }
    return NULL;
}

void debug_info(header_t *h, stream_t *t) {
    printf("header->verb = %s\n", h->verb);
    printf("header->path = %s\n", h->path);
    printf("header->version = %s\n", h->version);

    header_kv *kv;
    list *l = h->header_keys;
    _for_each(l, kv) {
        string *k = kv->key;
        string *v = kv->value;
        printf("    [%s]: \"%s\"\n", k->str, v->str);
    }
}

char *get_key(list *l, char *key) {
    header_kv *kv;
    _for_each(l, kv) {
        string *k = kv->key;
        if (!strncmp(k->str, key, k->length)) {
            string *v = kv->value;
            return strdup(v->str);
        }
    }
    return NULL;
}

header_t *stream_parser(stream_t *stream) {
    header_t *header = S(_header_t());
    CONTINUE_IF(skip_spaces(stream, MAX_LOOP));
    CONTINUE_IF(read_until_space(&header->verb, stream, MAX_LOOP));
    CONTINUE_IF(skip_spaces(stream, MAX_LOOP));
    CONTINUE_IF(read_until_space(&header->path, stream, MAX_PATH));
    CONTINUE_IF(skip_spaces(stream, MAX_LOOP));
    CONTINUE_IF(read_until_CRLF(&header->version, stream, MAX_LOOP));

    char *c = NULL;
    list *map = EMPTY;
    bool body_parse = false;
    size_t size_read = 0;
    bool application_url_encoded = false;
    char *bound = NULL; //for storing boundary on form-data
    while(size_read = read_until_CRLF(&c, stream, 8192)) {
        if (size_read == 8192) {
            // expected entry too large!
            header->err_code = 513;
            free(c);
            c = NULL;
            S(map);
            L(map);
            return header;
        }
        if (c[0] == 0) {
            free(c);
            c = NULL;
            break;
        } else if (!body_parse) {
            // CASE INSENSITIVE!!!
            // check for string being Content-Length or
            // check for string being Content-Type
            string *key = S(copy_until(c, ':'));
            char *val = c+(key->length+1);
            while(is_space(*val)) {
                val++;
            }
            if (key->str && !strncmp(key->str, "Content-Type", key->length)) {
                if (val && !strncmp(val, "application/x-www-form-url-encoded", 35)) {
                    application_url_encoded = true;
                } else {
                    bound = application_match(val, "multipart/form-data; boundary=");
                    char *rebound = calloc(sizeof(char), (strlen(bound) + 3));
                    rebound[0] = '-';
                    rebound[1] = '-';
                    memcpy(rebound+2, bound, strlen(bound));
                    free(bound);
                    bound = rebound;
                }
            }
            string *value = S(_string(strdup(val), 1));
            header_kv *header = _header_kv(key, value);
            map = _list(header, map);
            L(value);
            L(key);
            free(c);
            c = NULL;
        }
    }
    header->header_keys = S(map);
    if (bound) {
        char *val = get_key(header->header_keys, "Content-Length");
        size_t i = atoi(val);
        char *str = NULL;

        //debug_info(header, stream);
        set_stream_force_read_size(stream, i);
        printf("searching for [%s] in stream with size %i\n", bound, i);
        i = read_excluding(&str, stream, bound, 256000, false);
        free(str);
        if (i) {
            printf("ERROR: shouldn't have data before first multipart boundary");
        } else {
            char *JUNK = NULL;
            REPEAT(2) {
                size_t junk_size = read_excluding(&JUNK, stream, "\r\n", 1024, false);
                if (junk_size) {
                    printf("JUNK: [%s]\n", JUNK);
                }
                free(JUNK);
            }
            char c;
            pop_char(stream, &c);

            size_t first_data_size = read_excluding(&str, stream, bound, 256000, true);
            if (first_data_size) {
                puts("DATA read success, outputting to out.jpg");
                printf("[%i]\n", first_data_size);
                int fd = open("out.binary", O_WRONLY | O_CREAT);
                write(fd, str, first_data_size);
                close(fd);
            }
            free(str);
        }
        i = read_to_null(stream);
        free(bound);
    } else if (application_url_encoded) {
        // do this if application/x-www-form-url-encoded!!
        while(read_until_CRLF(&c, stream, 512)) {
            if (is_space(c[0])) {
                char *k = c;
                while(*k) {
                    k++;
                }
            }
            free(c);
            c = NULL;
        }
    }
    if (c) free(c);
    return header;
fail:
    L(header);
    return NULL;
}


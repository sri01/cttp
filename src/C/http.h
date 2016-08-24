#ifndef _HTTP_H_
#define _HTTP_H_

#include "cref/ref.h"
#include "cref/types.h"
#include "cref/list.h"

refstruct(url_prefix_tree, {
    string *prefix;
    list   *children;
    struct url_prefix_tree *_var;
    void (*handler)(int, list *);
});

typedef struct {
    int _port;
    int _socket;
    bool running;
    pthread_t _server_thread;
    url_prefix_tree *urls;
} http_t;



url_prefix_tree *lookup(url_prefix_tree *, string *, list **);
url_prefix_tree *_url_prefix_tree(string *prefix);
http_t *create_server(url_prefix_tree *, int);

void add_to_prefix_tree(url_prefix_tree *, string *, void (*)(int, list *));
void start_http_server(http_t *);
void http_end_write(int);

#endif
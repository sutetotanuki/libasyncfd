/*
 *  asyncsock.h
 *  libasyncsock
 *
 *  Created by Masatoshi Teruya on 13/02/19.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */
#ifndef ___ASYNC_SOCK___
#define ___ASYNC_SOCK___

#include <stdint.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#ifdef HAVE_CONFIG_H
#include "asyncsock_config.h"
#endif

static const int AS_YES = 1;

#define AS_TYPE_STREAM      SOCK_STREAM
#define AS_TYPE_DGRAM       SOCK_DGRAM
#define AS_TYPE_SEQPACKET   SOCK_SEQPACKET

typedef struct {
    // socket descriptor
    int32_t fd;
    // type of protocol: PF_UNIX|PF_INET
    int proto;
    // socket type: SOCK_STREAM|SOCK_DGRAM|SOCK_SEQPACKET
    int type;
    // opaque address byte length
    size_t addrlen;
    // opaque address 
    void *addr;
} asock_t;

/*
    create and return new asock_t
    
    addr: address-string must be following format;
            unix-addr: unix://path/to/sock/file
            inet-addr: inet://ipaddr:port
    
    len : length of addr
    type: AS_TYPE_STREAM|AS_TYPE_DGRAM|AS_TYPE_SEQPACKET
    
    return: new asock_t on success, or NULL on failure.(check errno)
*/
asock_t *asock_alloc( const char *addr, size_t len, int type );
/*
    deallocate asock_t
*/
void asock_dealloc( asock_t *as );
/*
    bind and listen
    
    backlog: pass for backlog argument of listen(2)
    
    return: 0 on success, or -1 on failure.(check errno)
*/
int asock_listen( asock_t *as, int backlog );


/*
    event loop state data structure(opaque)
*/
typedef struct _as_state_t as_state_t;
/*
    event loop data structure
*/
typedef struct {
    asock_t *as;
    as_state_t *state;
} as_loop_t;

/*
    state cleanup callback-function prototype.
    cleanup function will be call when deallocate as_loop.
*/
typedef void (*as_loop_cleanup_cb)( void* );
#define as_loop_cleanup_null    ((as_loop_cleanup_cb)0)
/*
    carete and return as_loop_t.
    
    as      : asock_t
    nevts   : default number of event buffer.(increase automatically)
    cb      : cleanup function. you can set to null.
    udata   : pass for arguemnt of as_cleanup_cb
    
    return: new as_loop_t on success, or NULL on failure.(check errno)
*/
as_loop_t *asock_loop_alloc( asock_t *as, int32_t nevts, as_loop_cleanup_cb cb, 
                             void *udata );
/*
    deallocate as_loop_t
*/
void asock_loop_dealloc( as_loop_t *loop );


/*
    event watch flags
*/
typedef enum {
    // event trigger flags that will use with event types.
    // level trigger
    AS_EV_LEVEL = 1 << 0,
    // edge trigger
    AS_EV_EDGE = 1 << 1,
    // event types
    // watch read event
    AS_EV_READ = 1 << 2,
    // watch write event
    AS_EV_WRITE = 1 << 3,
    // valid event watch flag
    AS_EVFLAG_VALID = ~(AS_EV_LEVEL|AS_EV_EDGE|AS_EV_READ|AS_EV_WRITE)
} as_evflag_e;

typedef struct _as_watch_t as_watch_t;
/*
    callback-function prototype of each event types
*/
typedef void (*as_watch_cb)( as_loop_t*, as_watch_t*, as_evflag_e );
/*
    event watch data structure
    
    fd      : descrictor(socket)
    flg_sys : internal event flag(do not touch)
    flg     : event type flag
    cb      : callback-function pointer
    udata   : user data pointer
*/
struct _as_watch_t {
    int fd;
#ifdef USE_KQUEUE
    int flg_kq;
#endif
    as_evflag_e flg;
    as_watch_cb cb;
    void *udata;
};

/*
    register socket descriptor to event loop.
    
    w       : empty watch data structure(mean not NULL)
    loop    : target event loop(non NULL)
    fd      : socket descriptor
    flg     : event type flag.(default level trigger)
                eg: if you want to watch read event with edge trigger;
                    AS_EV_READ|AS_EV_EDGE
    cb      : callback function on this event
    udata   : to set a udata of w(as_watch_t).
    
    return: 0 on success, -1 on failure.(check errno)
*/
int asock_watch( as_watch_t *w, as_loop_t *loop, int fd, as_evflag_e flg, 
                 as_watch_cb cb, void *udata );
/*
     ## currently internal use only
*/
int asock_rewatch( as_watch_t *w, as_loop_t *loop );
/*
    deregister as_watch_t from event loop.
    
    w   : target event watch data
    loop: registered event loop
*/
int asock_unwatch( as_watch_t *w, as_loop_t *loop );
/*
    close socket descriptor and then deregister as_watch_t from event loop.
    
    w   : target event watch data
    loop: registered event loop
*/
int asock_unwatch_close( as_watch_t *w, as_loop_t *loop );
/*
    wait a while occur event and specified timeout
    
    loop    : target event loop
    timeout : if set to NULL wait forever
*/
int asock_wait( as_loop_t *loop, struct timespec *timeout );


/* helper functions */

/*
    asock_accept(s,addr,len)
    this is accept wrapper.
    if your system have accept4() then use it and set non-block and cloexec 
    flags.
    
    s   : listening socket descriptor(int)
    addr: pointer to struct sockaddr
    len : pointer to size of addr.
*/

/*
    asock_accept_chk(c,delay)
    this will check returned socket descriptor from accept and 
    to set flags below;
        O_NONBLOCK and FD_CLOEXEC.(if your system does not support accept4)
        TCP_NODELAY. (if delay specified to 0)
*/
#ifdef HAVE_ACCEPT4

#define asock_accept(s,addr,len) \
    (accept4(s,addr,len,SOCK_NONBLOCK|SOCK_CLOEXEC))

#define asock_accept_chk(c,delay) \
    (c == -1) ? -1 : \
    ((delay) ? 1 : \
     !setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&AS_YES,(socklen_t)sizeof(AS_YES)))

#else

#define asock_accept(s,addr,len)    (accept(s,addr,len))

// ???: do i really need to set o_nonblock flags for portability?
//      some documents or comments on a web said that you don't need to set 
//      that flag when i use kqueue because kevent api will automatically set 
//      this flag. is that true? i couldn't find that on kernel source...
#define asock_accept_chk(c,delay) \
    (c == -1) ? -1 : \
    ((fcntl(c,F_SETFL,O_NONBLOCK) != -1) && \
     (fcntl(c,F_SETFD,FD_CLOEXEC) != -1) && \
     ((delay) ? 1 : !setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&AS_YES,sizeof(int))))

#endif

/*
    asock_edge_start()
    use this macro before retrieving client data(like read/recv) if you use 
    edge trigger event.
*/
#define asock_edge_start()
/*
    asock_edge_again()
    use this macro after retrieving client data(like read/recv) if you use 
    edge trigger event.
*/
#define asock_edge_again()
/*
    implements of asock_edge_start()/asock_edge_again() for epoll
*/
#ifdef USE_EPOLL
#undef asock_edge_start
#undef asock_edge_again
#define asock_edge_start()    __ASYNCSOCK_EDGE_AGAIN:
#define asock_edge_again()    goto __ASYNCSOCK_EDGE_AGAIN

#endif
    

#endif

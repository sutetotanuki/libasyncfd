/*
 *  asyncfd.h
 *  libasyncfd
 *
 *  Created by Masatoshi Teruya on 13/02/19.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */
#ifndef ___ASYNCFD___
#define ___ASYNCFD___

#include <stdint.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>
#include "libasyncfd_config.h"

#if USE_EPOLL
#include <sys/timerfd.h>
#endif

static const int AS_YES = 1;
static const int AS_NO = 0;

#define AS_TYPE_STREAM      SOCK_STREAM
#define AS_TYPE_DGRAM       SOCK_DGRAM
#define AS_TYPE_SEQPACKET   SOCK_SEQPACKET

typedef struct {
    // socket descriptor
    int32_t fd;
    // type of protocol family
    int family;
    // socket type: SOCK_STREAM|SOCK_DGRAM|SOCK_SEQPACKET
    int type;
    // type of protocol: PF_UNIX|PF_INET
    int proto;
    // opaque address byte length
    size_t addrlen;
    // opaque address 
    void *addr;
} afd_sock_t;

/*
    create and return new afd_sock_t
    
    addr: address-string must be following format;
            unix-addr: unix://path/to/sock/file
            inet-addr: inet://ipaddr:port
    
    len : length of addr
    type: AS_TYPE_STREAM|AS_TYPE_DGRAM|AS_TYPE_SEQPACKET
    
    return: new afd_sock_t on success, or NULL on failure.(check errno)
*/
afd_sock_t *afd_sock_alloc( const char *addr, size_t len, int type );
/*
    deallocate afd_sock_t
*/
void afd_sock_dealloc( afd_sock_t *as );
/*
    bind and listen
    
    backlog: pass for backlog argument of listen(2)
    
    return: 0 on success, or -1 on failure.(check errno)
*/
int afd_listen( afd_sock_t *as, int backlog );


/*
    event loop state data structure(opaque)
*/
typedef struct _afd_state_t afd_state_t;
/*
    event loop data structure
*/
typedef struct {
    afd_sock_t *as;
    afd_state_t *state;
} afd_loop_t;

/*
    state cleanup callback-function prototype.
    cleanup function will be call when deallocate afd_loop_t.
*/
typedef void (*afd_loop_cleanup_cb)( void* );
#define afd_loop_cleanup_null    ((afd_loop_cleanup_cb)0)
/*
    carete and return afd_loop_t.
    
    as      : afd_sock_t
    nevts   : default number of event buffer.(increase automatically)
    cb      : cleanup function. you can set to null.
    udata   : pass for arguemnt of as_cleanup_cb
    
    return: new afd_loop_t on success, or NULL on failure.(check errno)
*/
afd_loop_t *afd_loop_alloc( afd_sock_t *as, int32_t nevts, 
                            afd_loop_cleanup_cb cb, void *udata );
/*
    deallocate afd_loop_t
*/
void afd_loop_dealloc( afd_loop_t *loop );

/*
    run event loop forever
    
    loop    : target event loop
*/
int afd_loop( afd_loop_t *loop );

/*
    run event loop a once
    
    loop    : target event loop
    timeout : if set to NULL wait forever
*/
int afd_loop_once( afd_loop_t *loop, struct timespec *timeout );

/*
    stop running loop
    
    loop    : target event loop
*/
void afd_unloop( afd_loop_t *loop );


/*
    event watch flags
*/
typedef enum {
    // event trigger flags that will use with read or write event types.
    // default: level trigger
    // edge trigger
    AS_EV_EDGE = 1 << 0,
    // event types
    // watch read event
    AS_EV_READ = 1 << 1,
    // watch write event
    AS_EV_WRITE = 1 << 2,
    // watch timer event
    AS_EV_TIMER = 1 << 3,
    // valid event watch flag
    AS_EV_ISVALID = ~(AS_EV_EDGE|AS_EV_READ|AS_EV_WRITE)
} afd_evflag_e;

typedef struct _afd_watch_t afd_watch_t;
/* callback-function prototype of each event types*/
typedef void (*afd_watch_cb)( afd_loop_t *loop, afd_watch_t *w, 
                              afd_evflag_e flg, int hup );
/*
    event watch data structure
    
    fd      : descrictor
    flg     : event flag(AS_EV_READ or AS_EV_WRITE or AS_EV_TIMER)
    fflg    : event filter flag (internal use)
    filter  : event filter (internal use)
    tspec   : timeout interval for timer event (internal use)
    cb      : callback-function pointer (internal use)
    udata   : user data pointer
*/
struct _afd_watch_t {
    int fd;
    uint8_t flg;
    uint32_t fflg;
#if USE_KQUEUE
    int16_t filter;
    struct timespec tspec;
#elif USE_EPOLL
    uint32_t filter;
    struct itimerspec tspec;
#endif
    afd_watch_cb cb;
    void *udata;
};

/*
    initialize afd_watch_t for read/write event
    
    w       : empty watch data structure(mean not NULL)
    fd      : descriptor
    flg     : event type flag.(default level trigger)
                eg: if you want to watch read event with edge trigger;
                    AS_EV_READ|AS_EV_EDGE
    cb      : callback function on this event
    udata   : to set a udata of w(afd_watch_t)
    
    return: 0 on success, -1 on failure.(check errno)
*/
int afd_watch_init( afd_watch_t *w, int fd, afd_evflag_e flg, afd_watch_cb cb, 
                    void *udata );
/*
    initialize afd_watch_t for timer event
    
    w       : empty watch data structure(mean not NULL)
    tspec   : timeout interval
    cb      : callback function on this event
    udata   : to set a udata of w(afd_watch_t)
    
    return: 0 on success, -1 on failure.(check errno)
*/
int afd_timer_init( afd_watch_t *w, struct timespec *tspec, afd_watch_cb cb, 
                    void *udata );

/*
    update time interval for timer event
    
    w       : afd_watch_t for timer
    tspec   : timeout interval
*/
void afd_timer_update( afd_watch_t *w, struct timespec *tspec );

/*
    register afd_watch_t to event loop.
    
    loop: target event loop(non NULL)
    w   : initialized afd_watch_t pointer
    
    return: 0 on success, -1 on failure.(check errno)
*/
int afd_watch( afd_loop_t *loop, afd_watch_t *w );
/*
    register afd_watch_t to event loop.
    
    loop    : target event loop(non NULL)
    ...     : initialized afd_watch_t pointers and last argument must be NULL
    
    return: 0 on success, -1 on failure.(check errno)
*/
int afd_nwatch( afd_loop_t *loop, ... );

/*
    deregister afd_watch_t from event loop.
    
    loop    : target event loop
    closefd : 1 on close descriptor and deregistered w from event loop
    w       : registered afd_watch_t pointer
    
    return: 0 on success, or -1 on failure.(check errno)
*/
int afd_unwatch( afd_loop_t *loop, int closefd, afd_watch_t *w );
/*
    deregister afd_watch_t from event loop.
    
    loop: target event loop
    ... : registered afd_watch_t pointers and last argument must be NULL
    
    return: 0 on success, or less then 0 on failure.(check errno)
*/
int afd_unnwatch( afd_loop_t *loop, int closefd, ... );


/* helper functions */
#define afd_filefd_init(fd) \
    ((fcntl(fd,F_SETFL,O_NONBLOCK) != -1) && \
     (fcntl(fd,F_SETFD,FD_CLOEXEC) != -1))

#define afd_sockfd_init(fd) \
    (afd_filefd_init(fd) && \
     !setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&AS_YES,(socklen_t)sizeof(AS_YES)))

#define afd_socket(as) \
    (((as->fd = socket(as->family,as->type,as->proto)) > 0) && \
     afd_sockfd_init(as->fd))

/*
    afd_connect(as)
    connect wrapper.
    
    as   : afd_sock_t for connect
*/
#define afd_connect(as) \
    (!connect(as->fd,(struct sockaddr*)as->addr,(socklen_t)as->addrlen) || \
     (errno == EINPROGRESS))

/*
    afd_accept(c,s,addr,len,delay)
    afd_accept_unix(c,s,addr,len,delay) for unix domain socket
    
    NOTE: if your system have accept4() then use it and set non-block and 
          cloexec flags.
    
    c       : client socket pointer
    s       : listening socket descriptor(int)
    addr    : pointer to struct sockaddr
    len     : pointer to size of addr.
    
    afd_accept():
        delay   : set TCP_NODELAY (if delay specified to 0)
    
    return: 1 on success
            -1 on failure to accept client socket
            0 on failure to set flags
*/
#if HAVE_ACCEPT4

#define afd_accept(c,s,addr,len,delay) \
    ((*c = accept4(s,addr,len,SOCK_NONBLOCK|SOCK_CLOEXEC)) == -1 ) ? -1 : \
     (delay ? 1 : \
      !setsockopt(*c,IPPROTO_TCP,TCP_NODELAY,&AS_YES,(socklen_t)sizeof(AS_YES)))

#define afd_accept_unix(c,s,addr,len) \
    ((*c = accept4(s,addr,len,SOCK_NONBLOCK|SOCK_CLOEXEC) == -1 ) ? -1 : 1)

#else

// ???: do i really need to set o_nonblock flags for portability?
//      some documents or comments on a web said that you don't need to set 
//      that flag when i use kqueue because kevent api will automatically set 
//      this flag. is that true? i couldn't find that on kernel source...
#define afd_accept(c,s,addr,len,delay) \
    ((*c = accept(s,addr,len)) == -1 ) ? -1 : \
     (afd_filefd_init(*c) && (delay ? 1 : \
      !setsockopt(*c,IPPROTO_TCP,TCP_NODELAY,&AS_YES,sizeof(int))))

#define afd_accept_unix(c,s,addr,len) \
    ((*c = accept(s,addr,len)) == -1 ) ? -1 : (afd_filefd_init(*c))

#endif


#if USE_KQUEUE
/*
    afd_edge_start()
    use this macro before retrieving client data(like read/recv) if you use 
    edge trigger event.
*/
#define afd_edge_start()
/*
    afd_edge_again()
    use this macro after retrieving client data(like read/recv) if you use 
    edge trigger event.
*/
#define afd_edge_again()

#elif USE_EPOLL
/*
    implements of afd_edge_start()/afd_edge_again() for epoll
*/
#define afd_edge_start()    __ASYNCFD_EDGE_AGAIN:
#define afd_edge_again()    goto __ASYNCFD_EDGE_AGAIN

#endif
    

#endif

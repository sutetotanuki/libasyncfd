/*
 *  asyncfd.c
 *  libasyncfd
 *
 *  Created by Masatoshi Teruya on 13/02/19.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */

#include "libasyncfd.h"
#include "asyncfd_private.h"
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/un.h>

// FQDN maximum length:(include dot separator)
// FQDN(255) + null-terminator
#define ASYNCSOCK_FQDN_LEN          256
// inet socket max length: 
// FQDN(255) + port-string(1-65535 = 5 charactor) + null-terminator
#define ASYNCSOCK_INETPATH_MAX      261
// unix-domain socket max path length
#define ASYNCSOCK_UNIXPATH_MAX      sizeof(((struct sockaddr_un*)0)->sun_path)
// port range: 1-65535 + null-terminator
#define ASYNCSOCK_PORT_LEN          6

#define afd_sockfd_init(fd) \
    (afd_filefd_init(fd) && \
     !setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&AS_YES,(socklen_t)sizeof(AS_YES)))

static afd_sock_t *_asoc_alloc_inet( int type, const char *addr, size_t len )
{
    if( len < ASYNCSOCK_INETPATH_MAX )
    {
        // find port separator
        char *port = memchr( addr, ':', len );
        ptrdiff_t hlen = len;
        ptrdiff_t plen = 0;
        
        if( port )
        {
            // port-number undefined
            if( !port[1] ){
                errno = EINVAL;
                return NULL;
            }
            // calc host and port length
            hlen = (uintptr_t)port - (uintptr_t)addr;
            plen = len - hlen - 1;
            port++;
        }
        // cannot use wildcard ip-address if port number unspecified
        else if( *addr == '*' ){
            errno = EINVAL;
            return NULL;
        }
        
        // valid host and port length
        if( hlen < ASYNCSOCK_FQDN_LEN && plen < ASYNCSOCK_PORT_LEN )
        {
            const struct addrinfo hints = {
                // AI_PASSIVE:bind socket if node is null
                .ai_flags = AI_PASSIVE,
                // AF_INET:ipv4 | AF_INET6:ipv6
                .ai_family = AF_UNSPEC,
                // SOCK_STREAM:tcp | SOCK_DGRAM:udp | SOCK_SEQPACKET
                .ai_socktype = type,
                // IPPROTO_TCP:tcp | IPPROTO_UDP:udp | 0:automatic
                .ai_protocol = 0,
                // initialize
                .ai_addrlen = 0,
                .ai_canonname = NULL,
                .ai_addr = NULL,
                .ai_next = NULL
            };
            struct addrinfo *res = NULL;
            int rc = 0;
            
            // getaddrinfo is better than inet_pton.
            // i wonder that can be ignore an overhead of creating socket
            // descriptor when i simply want to confirm correct address?
            // wildcard ip-address
            if( *addr == '*' ){
                rc = getaddrinfo( NULL, port, &hints, &res );
            }
            else {
                char host[hlen];
                memcpy( host, addr, hlen );
                host[hlen] = 0;
                rc = getaddrinfo( host, port, &hints, &res );
            }

            if( rc == 0 )
            {
                afd_sock_t *as = NULL;
                struct addrinfo *ptr = res;
                int fd = 0;
                
                errno = 0;
                do
                {
                    // try to create socket descriptor for find valid address
                    if( ( fd = socket( ptr->ai_family, ptr->ai_socktype, 
                                       ptr->ai_protocol ) ) != -1 )
                    {
                        // init socket descriptor
                        if( afd_sockfd_init( fd ) )
                        {
                            struct sockaddr_in *inaddr = palloc( struct sockaddr_in );
                            
                            if( inaddr && ( as = palloc( afd_sock_t ) ) ){
                                as->fd = fd;
                                as->proto = PF_INET;
                                as->type = type;
                                as->addrlen = ptr->ai_addrlen;
                                as->addr = (void*)inaddr;
                                // copy struct sockaddr
                                memcpy( (void*)inaddr, (void*)ptr->ai_addr, 
                                        (size_t)ptr->ai_addrlen );
                                break;
                            }
                            else if( inaddr ){
                                pdealloc( inaddr );
                            }
                        }
                        close( fd );
                        break;
                    }

                } while( ( ptr = ptr->ai_next ) );
                
                // remove address-list
                freeaddrinfo( res );
                return as;
            }
            
            return NULL;
        }
    }
    
    errno = ENAMETOOLONG;
    
    return NULL;
}

static afd_sock_t *_afd_alloc_unix( int type, const char *path, size_t len )
{
    // length too large
    if( len < ASYNCSOCK_UNIXPATH_MAX )
    {
        // create socket descriptor
        int fd = socket( PF_UNIX, type, 0 );
        
        if( fd != -1 )
        {
            // init socket descriptor
            if( afd_sockfd_init( fd ) )
            {
                afd_sock_t *as = palloc( afd_sock_t );
                struct sockaddr_un *unaddr = NULL;
                
                if( as && ( unaddr = palloc( struct sockaddr_un ) ) ){
                    as->fd = fd;
                    as->proto = PF_UNIX;
                    as->type = type;
                    as->addrlen = sizeof( struct sockaddr_un ) + len;
                    as->addr = (void*)unaddr;
                    unaddr->sun_family = AF_UNIX;
                    memcpy( (void*)unaddr->sun_path, (void*)path, len );
                    unaddr->sun_path[len] = 0;
                    return as;
                }
                else if( as ){
                    pdealloc( as );
                }
            }
            close( fd );
        }
    }
    else {
        errno = ENAMETOOLONG;
    }
    
    return NULL;
}

afd_sock_t *afd_sock_alloc( const char *addr, size_t len, int type )
{
    char *delim = (char*)memchr( addr, ':', len );
    
    // valid length, found delimitor, valid format
    if( len > 8 && ( delim = (char*)memchr( addr, ':', len ) ) &&
        delim[1] == '/' && delim[2] == '/' && delim[3] )
    {
        // calc remain
        len -= ( (uintptr_t)delim - (uintptr_t)addr ) + 3;
        // skip separator [://]
        delim += 3;
        // check scheme
        // inet addr
        if( strncmp( "inet", addr, 4 ) == 0 ){
            return _asoc_alloc_inet( type, delim, len );
        }
        // unix domain socket
        else if( strncmp( "unix", addr, 4 ) == 0 ){
            return _afd_alloc_unix( type, delim, len );
        }
        // unknown scheme
        // errno = EPROTONOSUPPORT;
    }
    
    errno = EINVAL;
    return NULL;
}


void afd_sock_dealloc( afd_sock_t *as )
{
    if( as->fd ){
        close( as->fd );
    }
    // remove unix domain socket file
    if( as->proto == PF_UNIX && as->addr ){
        unlink( ((struct sockaddr_un*)as->addr)->sun_path );
    }
    if( as->addr ){
        pdealloc( as->addr );
    }
    pdealloc( as );
}

int afd_listen( afd_sock_t *as, int backlog )
{
    // bind and listen
    if( !bind( as->fd, (struct sockaddr*)as->addr, (socklen_t)as->addrlen ) &&
        !listen( as->fd, backlog ) ){
        return 0;
    }
    
    return -1;
}



struct _afd_state_t {
#ifdef USE_KQUEUE
    struct kevent *rcv_evs;

#elif USE_EPOLL
    struct epoll_event *rcv_evs;
#endif
    int32_t nrcv;
    int32_t nreg;
    int32_t fd;
    afd_loop_cleanup_cb cleanup;
    void *udata;
};


static afd_state_t *_afd_state_alloc( int32_t nevs, afd_loop_cleanup_cb cb, 
                                       void *udata )
{
    afd_state_t *state = palloc( afd_state_t );
    
    if( state )
    {
        if(
#ifdef USE_KQUEUE
        ( state->rcv_evs = pnalloc( nevs, struct kevent ) ) && 
        ( state->fd = kqueue() ) != -1

#elif USE_EPOLL
        ( state->rcv_evs = pnalloc( nevs, struct epoll_event ) ) && 
#ifdef HAVE_EPOLL_CREATE1
        ( state->fd = epoll_create1( EPOLL_CLOEXEC ) ) != -1
#else
        ( state->fd = epoll_create( nevs ) ) != -1
#endif
#endif
        ){
            state->nrcv = nevs;
            state->nreg = 0;
            state->cleanup = cb;
            return state;
        }
        else if( state->rcv_evs ){
            pdealloc( state->rcv_evs );
        }
        pdealloc( state );
    }
    
    return NULL;
}

static int _afd_state_realloc( afd_state_t *state, int32_t nevs )
{
#ifdef USE_KQUEUE
    struct kevent *evs = pnalloc( nevs, struct kevent );
#elif USE_EPOLL
    struct epoll_event *evs = pnalloc( nevs, struct epoll_event );
#endif
    
    if( evs ){
        pdealloc( state->rcv_evs );
        state->rcv_evs = evs;
        state->nrcv = nevs;
        return 0;
    }
    
    return -1;
}

static void _afd_state_dealloc( afd_state_t *state )
{
    afd_loop_cleanup_cb cb = state->cleanup;
    void *udata = state->udata;
    
    close( state->fd );
    pdealloc( state->rcv_evs );
    pdealloc( state );
    
    // call user cleanup code
    if( cb ){
        cb( udata );
    }
}


afd_loop_t *afd_loop_alloc( afd_sock_t *as, int32_t nevts, 
                            afd_loop_cleanup_cb cb, void *udata )
{
    if( nevts > 0 )
    {
        afd_loop_t *loop = palloc( afd_loop_t );
        
        if( loop && ( loop->state = _afd_state_alloc( nevts, cb, udata ) ) ){
            loop->as = as;
            return loop;
        }
        
        pdealloc( loop );
    }
    else {
        errno = EINVAL;
    }
    
    return NULL;
}

void afd_loop_dealloc( afd_loop_t *loop )
{
    _afd_state_dealloc( loop->state );
    pdealloc( loop );
}

int afd_watch_init( afd_watch_t *w, int fd, afd_evflag_e flg, afd_watch_cb cb, 
                    void *udata )
{
    // valid defined descriptor, flag, callbacks
    if( fd > 0 && flg && ( flg & AS_EV_ISVALID ) == 0 && cb )
    {
        // set passed args
        w->fd = fd;
        w->flg = 0;
        w->cb = cb;
        w->udata = udata;
        
        // init filter
        // kqueue will catch hang-up event on default.
        if( flg & AS_EV_EDGE ){
            flg &= ~AS_EV_EDGE; 
#ifdef USE_KQUEUE
            w->flg = EV_ADD|EV_CLEAR;
#elif USE_EPOLL
            w->filter = EPOLLRDHUP|EPOLLET;
#endif
        }
        else {
#ifdef USE_KQUEUE
            w->flg = EV_ADD;
#elif USE_EPOLL
            w->filter = EPOLLRDHUP;
#endif
        }
        
        switch( flg )
        {
            case AS_EV_READ:
#ifdef USE_KQUEUE
                w->filter = EVFILT_READ;
#elif USE_EPOLL
                w->filter |= EPOLLIN;
#endif
            break;
            case AS_EV_WRITE:
#ifdef USE_KQUEUE
                w->filter = EVFILT_WRITE;
#elif USE_EPOLL
                w->filter |= EPOLLOUT;
#endif
            break;
            // invalid argument
            default:
                errno = EINVAL;
                return -1;
        }

        return 0;
    }
    
    // invalid arguments
    errno = EINVAL;
    
    return -1;
}

int afd_timer_init( afd_watch_t *w, struct timespec *tspec, afd_watch_cb cb, 
                    void *udata )
{
    // valid defined descriptor, flag, callbacks
    if( tspec && cb )
    {
        // set passed args
        w->cb = cb;
        w->udata = udata;
        
#ifdef USE_KQUEUE
        w->fd = 0;
        w->filter = EVFILT_TIMER;
        w->flg = EV_ADD;
        w->tspec = (struct timespec){ 
            .tv_sec = tspec->tv_sec,
            .tv_nsec = tspec->tv_nsec
        };
#elif USE_EPOLL
        w->flg = AS_EV_TIMER;
        w->filter = EPOLLRDHUP|EPOLLIN;
        w->tspec.it_value = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 };
        w->tspec.it_interval = (struct timespec){
            .tv_sec = tspec->tv_sec,
            .tv_nsec = tspec->tv_nsec
        };
        w->fd = timerfd_create( CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC );
        if( w->fd == -1 ){
            return -1;
        }
#endif
        return 0;
    }
    // invalid arguments
    errno = EINVAL;
    
    return -1;
}

int afd_watch( afd_loop_t *loop, afd_watch_t *w )
{
    int rc = 0;
#ifdef USE_KQUEUE
    struct kevent evt;
    
    if( w->filter == EVFILT_TIMER ){
        EV_SET( &evt, (uintptr_t)w, EVFILT_TIMER, w->flg, NOTE_NSECONDS, 
                w->tspec.tv_sec * 1000000000 + w->tspec.tv_nsec, (void*)w );
    }
    else {
        EV_SET( &evt, w->fd, w->filter, w->flg, 0, 0, (void*)w );
    }
    // register event
    rc = kevent( loop->state->fd, &evt, 1, NULL, 0, NULL );

#elif USE_EPOLL
    struct epoll_event evt;
    
    if( w->flg & AS_EV_TIMER )
    {
        struct timespec cur;
        
        // get current time
        if( clock_gettime( CLOCK_MONOTONIC, &cur ) == -1 ){
            return -1;
        }
        // set first invocation time
        w->tspec.it_value = (struct timespec){ 
            .tv_sec = cur.tv_sec +  w->tspec.it_interval.tv_sec,
            .tv_nsec = cur.tv_nsec + w->tspec.it_interval.tv_nsec
        };
        if( timerfd_settime( w->fd, TFD_TIMER_ABSTIME, &w->tspec, NULL ) == -1 ){
            return -1;
        }
    }
    // set udata pointer
    evt.data.ptr = (void*)w;
    evt.events = w->filter;
    // register event
    rc = epoll_ctl( loop->state->fd, EPOLL_CTL_ADD, w->fd, &evt );
#endif
    if( !rc ){
        loop->state->nreg++;
    }
    
    return rc;
}

int afd_nwatch( afd_loop_t *loop, ... )
{
    afd_watch_t *w = NULL;
    va_list args;
    
    va_start( args, loop );
    while( ( w = va_arg( args, afd_watch_t* ) ) )
    {
        if( afd_watch( loop, w ) == -1 ){
            va_end( args );
            return -1;
        }
    }
    va_end( args );
    
    return 0;
}

int afd_unwatch( afd_loop_t *loop, int closefd, afd_watch_t *w )
{
#ifdef USE_KQUEUE
    struct kevent evt;
    
    // use address for ident if kqueue timer event
    if( w->filter == EVFILT_TIMER ){
        EV_SET( &evt, (uintptr_t)w, w->filter, EV_DELETE, 0, 0, NULL );
    }
    else
    {
        EV_SET( &evt, w->fd, w->filter, EV_DELETE, 0, 0, NULL );
        if( closefd ){
            shutdown( w->fd, SHUT_RDWR );
            close( w->fd );
        }
    }
    
    // deregister event
    if( kevent( loop->state->fd, &evt, 1, NULL, 0, NULL ) == -1 ){
        return -1;
    }
    
#elif USE_EPOLL
    struct epoll_event evt;
    
    // deregister event
    // do not set null to event argument for portability
    if( epoll_ctl( loop->state->fd, EPOLL_CTL_DEL, w->fd, &evt ) == -1 ){
        return -1;
    }
    // use timerfd with epoll
    else if( closefd ){
        close( w->fd );
    }
#endif
    
    // decrement number of registered event
    loop->state->nreg--;
    
    return 0;
}

int afd_unnwatch( afd_loop_t *loop, int closefd, ... )
{
    int rc = 0;
    afd_watch_t *w = NULL;
    va_list args;
    
    va_start( args, closefd );
    while( ( w = va_arg( args, afd_watch_t* ) ) ){
        rc += afd_unwatch( loop, closefd, w );
    }
    va_end( args );
    
    return rc;
}

int afd_wait( afd_loop_t *loop, struct timespec *timeout )
{
    afd_state_t *state = loop->state;
    
    // realloc receive events container
    if( state->nreg > state->nrcv && 
        _afd_state_realloc( state, state->nreg ) == -1 ){
        return -1;
    }
    else
    {
#ifdef USE_KQUEUE
        struct kevent *evt = NULL;
        int nevt = kevent( state->fd, NULL, 0, state->rcv_evs, state->nreg, 
                           timeout );

#elif USE_EPOLL
        struct epoll_event *evt = NULL;
        int tval = ( !timeout ) ? -1 :
                     timeout->tv_sec * 1000 + timeout->tv_nsec * 1000;
        int nevt = epoll_pwait( state->fd, state->rcv_evs, state->nreg, tval, 
                              NULL );
#endif
        
        if( nevt > 0 )
        {
            afd_watch_t *w;
            int i;
            for( i = 0; i < nevt; i++ )
            {
                evt = &state->rcv_evs[i];
                
#ifdef USE_KQUEUE
                w = (afd_watch_t*)evt->udata;
                switch ( evt->filter ) {
                    case EVFILT_READ:
                        w->cb( loop, w, AS_EV_READ, evt->flags & EV_EOF );
                    break;
                    case EVFILT_WRITE:
                        w->cb( loop, w, AS_EV_WRITE, evt->flags & EV_EOF );
                    break;
                    case EVFILT_TIMER:
                        w->cb( loop, w, AS_EV_TIMER, 0 );
                    break;
                    default:
                        plog( "unknown event" );
                        break;
                }
#elif USE_EPOLL
                w = (afd_watch_t*)evt->data.ptr;
                if( evt->events & EPOLLIN ){
                    w->cb( loop, w, AS_EV_READ, 
                           evt->events & (EPOLLERR|EPOLLRDHUP|EPOLLHUP) );
                }
                else if( evt->events & EPOLLOUT ){
                    w->cb( loop, w, AS_EV_WRITE, 
                           evt->events & (EPOLLERR|EPOLLRDHUP|EPOLLHUP) );
                }
                else {
                    plog( "unknown event" );
                }
#endif
            }
        }
        
        return nevt;
    }
}


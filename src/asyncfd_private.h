/*
 *  asyncfd_private.h
 *  libasyncfd
 *
 *  Created by Masatoshi Teruya on 13/02/19.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */

#ifndef ___ASYNCFD_PRIVATE___
#define ___ASYNCFD_PRIVATE___

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "libasyncfd_config.h"

#if USE_KQUEUE
#include <sys/event.h>

#elif USE_EPOLL
#include <sys/epoll.h>

#else
#error("unsupported system")
#endif

// memory alloc/dealloc
#define palloc(t)       (t*)malloc( sizeof(t) )
#define pnalloc(n,t)    (t*)malloc( n * sizeof(t) )
#define pcalloc(n,t)    (t*)calloc( n, sizeof(t) )
#define prealloc(n,t,p) (t*)realloc( p, n * sizeof(t) )
#define pdealloc(p)     free((void*)p)

// print function error log
#define _pfelog(f,fmt,...) \
    printf( "failed to " #f "(): %s - " fmt "\n", \
            ( errno ) ? strerror(errno) : "", ##__VA_ARGS__ )
#define pfelog(f,...) _pfelog(f,__VA_ARGS__)

#define plog(fmt,...) printf( fmt "\n", ##__VA_ARGS__ )
#define pelog(fmt,...) \
    printf( fmt " : %s\n", ##__VA_ARGS__, ( errno ) ? strerror(errno) : "" )

#endif

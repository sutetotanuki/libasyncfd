/*
 *  asyncfd_private.h
 *  libasyncfd
 *
 *  Created by Masatoshi Teruya on 13/02/19.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */

#ifndef ___ASYNC_SOCK_PRIVATE___
#define ___ASYNC_SOCK_PRIVATE___

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include "asyncfd_config.h"
#endif

#ifdef USE_KQUEUE
#include <sys/event.h>

#elif USE_EPOLL
#include <sys/epoll.h>

#else
#error("unsupported system")
#endif

// memory alloc/dealloc
#define palloc(t)     (t*)malloc( sizeof(t) )
#define pnalloc(n,t)     (t*)malloc( n * sizeof(t) )
#define pcalloc(n,t)  (t*)calloc( n, sizeof(t) )
#define pdealloc(p)   free((void*)p)

// print function error log
#define _pfelog(f,fmt,...) \
    printf( "failed to " #f "(): %s - " fmt "\n", \
            ( errno ) ? strerror(errno) : "", ##__VA_ARGS__ )
#define pfelog(f,...) _pfelog(f,__VA_ARGS__)

#define plog(fmt,...) printf( fmt "\n", ##__VA_ARGS__ )
#define pelog(fmt,...) \
    printf( fmt " : %s\n", ##__VA_ARGS__, ( errno ) ? strerror(errno) : "" )

#endif

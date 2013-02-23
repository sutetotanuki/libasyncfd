#include <stdio.h>
#include "libasyncfd.h"
#include "asyncfd_private.h"
#include <string.h>
#include <unistd.h>
#include <signal.h>

static const char SENDTEST[] = 
        "HTTP/1.1 200 OK\r\n"
        "Server: libasyncfd\r\n"
        "Content-Length: 5\r\n"
        "Connection: keep-alive\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "hello";
static const size_t SENDTEST_LEN = sizeof( SENDTEST ) - 1;

typedef struct {
    int write;
    afd_watch_t read_w;
    afd_watch_t write_w;
} mydata_t;

static void test_unwatch( afd_loop_t *loop, afd_watch_t *w )
{
    afd_unwatch_close( w, loop );
    pdealloc( w->udata );
}

static void test_write( afd_loop_t *loop, afd_watch_t *w, afd_evflag_e flg )
{
    //plog( "test_write: %d", w->fd );
    mydata_t *data = (mydata_t*)w->udata;
    // recv-q test
    //usleep( 100 );
    if( data->write )
    {
        if( send( w->fd, SENDTEST, SENDTEST_LEN, 0 ) != SENDTEST_LEN ){
            pfelog( send );
            test_unwatch( loop, w );
        }
        else {
            data->write = 0;
        }
    }
}

static void test_read( afd_loop_t *loop, afd_watch_t *w, afd_evflag_e flg, int hup )
{
    char buf[8192];
    size_t blen = 8192;
    ssize_t len = read( w->fd, buf, blen );
    mydata_t *data = (mydata_t*)w->udata;
    
    if( len > 0 )
    {
        buf[len] = 0;
        //plog( "test_read: %d", w->fd );
        data->write = 1;
        // recv-q test
        //usleep( 100 );
    }
    // close by peer
    else if( len == 0 ){
        //plog( "close by peer: %d", w->fd );
        test_unwatch( loop, w );
    }
    else if( errno != EAGAIN || errno != EWOULDBLOCK ){
        pfelog( read );
        test_unwatch( loop, w );
    }

}

static void test_rw( afd_loop_t *loop, afd_watch_t *w, afd_evflag_e flg, int hup )
{
    // close by peer
    if( hup ){
        plog( "hup: close by peer: %d", w->fd );
        test_unwatch( loop, w );
    }
    else
    {
        mydata_t *data = (mydata_t*)w->udata;
        char buf[8192];
        size_t blen = 8192;
        ssize_t len = 0;
        
        afd_edge_start();
        len = read( w->fd, buf, blen );
        
        if( len > 0 )
        {
            buf[len] = 0;
            //plog( "test_read: %d", w->fd );
            data->write = 1;
            // recv-q test
            //usleep( 100 );
            if( send( w->fd, SENDTEST, SENDTEST_LEN, 0 ) != SENDTEST_LEN ){
                pfelog( send );
                test_unwatch( loop, w );
            }
            else {
                afd_edge_again();
            }
            /*
            else if( afd_rewatch( w, loop ) ){
                pfelog( afd_rewatch );
                test_unwatch( loop, w );
            }*/
        }
        // close by peer
        else if( len == 0 ){
            plog( "read close by peer: %d", w->fd );
            test_unwatch( loop, w );
        }
        else if( errno != EAGAIN || errno != EWOULDBLOCK ){
            test_unwatch( loop, w );
        }
        //else {
        //    pfelog( read, "back to event" );
        //}
    }
}

static void test_accept( afd_loop_t *loop, afd_watch_t *w, afd_evflag_e flg, int hup )
{
    int cfd = afd_accept( w->fd, NULL, NULL );
    mydata_t *data = NULL;
    
    switch( afd_accept_chk( cfd, 0 ) )
    {
        case -1:
            pfelog( accept );
        break;
        case 0:
            pfelog( afd_accept_check );
            close( cfd );
        break;
        default:
            //plog( "got client: %d", cfd );
            if( !( data = pcalloc( 1, mydata_t ) ) || 
                afd_watch( &data->read_w, loop, cfd, AS_EV_READ|AS_EV_EDGE, test_rw, data ) /*||
                afd_watch( &data->read_w, loop, cfd, AS_EV_READ, test_read, data ) ||
                afd_watch( &data->write_w, loop, cfd, AS_EV_WRITE, test_write, data )*/ ){
                if( data ){
                    pdealloc( data );
                }
                pelog( "failed to palloc/afd_watch" );
                close( cfd );
            }
    }
}

static int child_signal( void )
{
    int rc = 0;
    sigset_t ss;
    
    // set all-signal
    if( ( rc = sigfillset( &ss ) ) != 0 ){
        pfelog( sigfillset );
        exit(0);
    }
    else if( ( rc = sigprocmask( SIG_UNBLOCK, &ss, NULL ) ) != 0 ){
        pfelog( sigprocmask );
        exit(0);
    }
    /*
    else if( ( rc = sigwait( &ss, &signo ) ) != 0 ){
        pfelog( sigwait );
    }
    else {
        plog( "catch signal: %d", signo );
    }
    */
    
    return rc;
}

static void test_loop( afd_sock_t *as )
{
    afd_loop_t *loop = NULL;
    afd_watch_t w;
    
    child_signal();
    
    if( !( loop = afd_loop_alloc( as, SOMAXCONN, afd_loop_cleanup_null, NULL ) ) ){
        pfelog( afd_loop_alloc );
        exit(0);
    }
    else if( afd_watch( &w, loop, as->fd, AS_EV_READ, test_accept, NULL ) == -1 ){
        pfelog( afd_watch );
        exit(0);
    }
    else
    {
        //struct timeval tval = { 1, 0 };
        int nev;
        
        while( ( nev = afd_wait( loop, /*&tval*/NULL ) ) != -1 ){
            //plog( "proc ev: %d", nev );
        }
        pfelog( afd_wait );
        
        afd_unwatch( &w, loop );
        afd_loop_dealloc( loop );
        
        exit(0);
    }
}

static void test_listen( void )
{
    const char *addr = "inet://127.0.0.1:8080";
    afd_sock_t *as = afd_sock_alloc( addr, strlen( addr ), AS_TYPE_STREAM );
    int8_t nchild = 2;
    int8_t i = 0;
    pid_t pid = 0;
    
    if( !as ){
        pfelog( afd_alloc );
        exit(0);
    }
    else if( afd_listen( as, SOMAXCONN ) == -1 ){
        pfelog( afd_listen );
        exit(0);
    }
    
    for(; i < nchild; i++ )
    {
        if( ( pid = fork() ) == -1 ){
            pfelog( fork );
            exit(0);
        }
        else if( !pid ){
            test_loop( as );
        }
    }
    
    plog( "startup" );
    plog( "try to ab -c 10 -n 100000 -k http://127.0.0.1:8080/" );
    afd_sock_dealloc( as );
}

static int wait4signal( void )
{
    int rc = 0;
    int signo = 0;
    sigset_t ss;
    
    // set all-signal
    if( ( rc = sigfillset( &ss ) ) != 0 ){
        pfelog( sigfillset );
    }
    /*else if( ( rc = sigprocmask( SIG_BLOCK, &ss, NULL ) ) != 0 ){
        pfelog( sigprocmask );
    }*/
    else if( ( rc = sigwait( &ss, &signo ) ) != 0 ){
        pfelog( sigwait );
    }
    else {
        plog( "catch signal: %d", signo );
        kill( 0, signo );
    }
    
    return rc;
}


int main (int argc, const char * argv[]) 
{
    // insert code here...
    test_listen();
    wait4signal();
    
    return 0;
}

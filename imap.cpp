#define FUSE_USE_VERSION 30

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <climits>
#include <csignal>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <set>

#define _FILE_OFFSET_BITS 64
#include <fuse.h>

#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>

#include "crash_handler.h"
#include "stack_trace.h"
#include "log.h"
#include "fs_log.h"
#include "imapfs.h"


const char* BUILD_VERSION = "0";

static CrashHandler& _ch = CrashHandler::instance();

using namespace std;

IMAPFS* _fs = NULL;

static void* imap_init(struct fuse_conn_info* conn)
{
    (void) conn;
    _fs = new IMAPFS("localhost", 2983, "tim", "v3rlYnu");
    return _fs;
}

static int imap_getattr(const char* path, struct stat* status)
{
    return _fs->getattr(path, status);
}


static int imap_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
    struct fuse_file_info* fi)
{
    (void) fi;
    return _fs->readdir(path, buf, filler, offset);

}

static int imap_open(const char* path, struct fuse_file_info* fi)
{
    // open is pretty much a NOOP for us
    LOGFN(LOG, INFO) << "open " << path;
    return 0;
}

static int imap_read(const char* path, char* buf, size_t size, off_t offset,
		    struct fuse_file_info* fi)
{
    return _fs->read(path, buf, size, offset, fi);
}

static int imap_write(const char* path, const char* buf, size_t size,
		     off_t offset, struct fuse_file_info* fi)
{
    return _fs->write(path, buf, size, offset, fi);
}

static int imap_statfs(const char* path, struct statvfs* stbuf)
{
    return _fs->statfs(path, stbuf);
}

static int imap_mknod(const char* path, mode_t mode, dev_t rdev)
{
    (void) rdev;

    return _fs->mknod(path, mode);
}

static int imap_fallocate(const char* path, int mode,
			off_t offset, off_t length, struct fuse_file_info* fi)
{
    return _fs->fallocate(path, mode, offset, length, fi);
}

static int imap_truncate(const char* path, off_t size)
{
    return _fs->truncate(path, size);
}

static int imap_fsync(const char* path, int isdatasync, struct fuse_file_info* fi)
{
    return _fs->fsync(path, isdatasync, fi);
}

static int imap_access(const char* path, int mask)
{
    return _fs->access(path, mask);
}

static int imap_unlink(const char* path)
{
	return _fs->unlink(path);
}

static int imap_mkdir(const char* path, mode_t mode)
{
    (void) mode;

    LOGFN(LOG, INFO) << "mkdir " << path;
    return 0;
}

static int imap_rmdir(const char* path)
{
    LOGFN(LOG, INFO) << "rmdir " << path;
    return 0;
}

static int imap_release(const char* path, struct fuse_file_info* fi)
{
    (void) path;
    (void) fi;

    LOGFN(LOG, INFO) << "release " << path;
    return 0;
}

static int imap_chmod(const char* path, mode_t mode)
{
    (void) mode;

    LOGFN(LOG, INFO) << "chmod " << path << " to " << mode;
    return 0;
}

static int imap_chown(const char* path, uid_t uid, gid_t gid)
{
    LOGFN(LOG, INFO) << "chown " << path << " to " << uid << " " << gid;
    return 0;
}


struct fuse_chan* _fc = NULL;
void sighandler(int signum, siginfo_t* info, void* context)
{
    signal(signum, SIG_DFL);
    LOGFN(LOG, DEBUG) << "unmounting " << signum;
    fuse_unmount("test", _fc);
    raise(signum);
}

int main(int argc, char* argv[])
{
    LOG::setLogLevel(DEBUG);
    
    // hook up all the FUSE callbacks
    struct fuse_operations imap_oper = { };
    imap_oper.init = imap_init;
    imap_oper.getattr = imap_getattr;
    imap_oper.readdir = imap_readdir;
    imap_oper.read = imap_read;
    imap_oper.open = imap_open;
    imap_oper.statfs = imap_statfs;
    imap_oper.write = imap_write;
    imap_oper.mkdir = imap_mkdir;
    imap_oper.rmdir = imap_rmdir;
    imap_oper.access = imap_access;
    imap_oper.mknod = imap_mknod;
    imap_oper.fallocate = imap_fallocate;
    imap_oper.truncate = imap_truncate;
    imap_oper.release = imap_release;
    imap_oper.fsync = imap_fsync;
    imap_oper.chmod = imap_chmod;
    imap_oper.chown = imap_chown;
    imap_oper.unlink = imap_unlink;

//    .readlink = imap_readlink,
//    .symlink = imap_symlink,
//    .link = imap_link,
//    .rename = imap_rename,

#ifdef HAVE_UTIMENSAT
//    .utimens = imap_utimens,
#endif
#ifdef HAVE_SETXATTR
//    .setxattr = imap_setxattr,
//    .getxattr = imap_getxattr,
//    .listxattr = imap_listxattr,
//    .removexattr = imap_removexattr,
#endif

    umask(0);

    LOG(LOG, INFO) << "starting imapfs";

    // setup vmime POSIX handler for all the vmime goodness
    vmime::platform::setHandler<vmime::platforms::posix::posixHandler>();
    
    // let's trap SIGINT so we can Ctrl-C out of this sucker when debugging,
    // and have the mounted filesystem unmounted instead of having to do it
    // manually all the time
    struct sigaction handle;
    handle.sa_sigaction = sighandler;
    sigemptyset(&handle.sa_mask);
    handle.sa_flags = SA_SIGINFO;  
    sigaction(SIGINT, &handle, NULL);

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    fuse_opt_parse(&args, NULL, NULL, NULL);
    fuse_opt_add_arg(&args, "-oallow_other");
    _fc = fuse_mount("test", &args);
    if (!_fc) {
        LOG(LOG, CRIT) << "could not mount";
        exit(-1);
    }
    struct fuse* fuse = fuse_new(_fc, &args, &imap_oper, sizeof(struct fuse_operations), NULL);
    if (!fuse) {
        LOG(LOG, CRIT) << "couldn't initialize filesystem";
        fuse_unmount("test", _fc);
        exit(-1);
    }
    int r = -1;
    try {
        r = fuse_loop(fuse);
        LOG(LOG, DEBUG) << "exiting";
        fuse_unmount("test", _fc);
    }
    catch (vmime::exception& e) {
        STACKFN(LOG, CRIT) << "uncaught vmime exception " << e;
    }
    return r;
}

#if 0
static int imap_readlink(const char* path, char* buf, size_t size)
{
    int res;

	res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}



static int imap_symlink(const char* from, const char* to)
{
    int res;

    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int imap_rename(const char* from, const char* to, unsigned int flags)
{
    int res;

    if (flags)
        return -EINVAL;

    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int imap_link(const char* from, const char* to)
{
    int res;

    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}


#ifdef HAVE_UTIMENSAT
static int imap_utimens(const char* path, const struct timespec ts[2], struct fuse_file_info* fi)
{
    (void) fi;
    int res;

    // don't use utime/utimes since they follow symlinks
    res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
        return -errno;

    return 0;
}
#endif

#ifdef HAVE_SETXATTR
// xattr operations are optional and can safely be left unimplemented */
static int imap_setxattr(const char* path, const char* name, const char* value, size_t size, int flags)
{
    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

static int imap_getxattr(const char* path, const char *name, char* value, size_t size)
{
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int imap_listxattr(const char* path, char* list, size_t size)
{
    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int imap_removexattr(const char* path, const char* name)
{
    int res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif

#endif

/*
typedef struct mail_overview {
  char *subject;		// message subject string
  ADDRESS *from;		// originator address list
  char *date;			// message composition date string
  char *message_id;		// message ID
  char *references;		// USENET references
  struct {			// may be 0 or NUL if unknown/undefined
    unsigned long octets;	// message octets (probably LF-newline form)
    unsigned long lines;	// message lines
    char *xref;			// cross references
  } optional;
} OVERVIEW;
*/

/* struct stat {
     dev_t     st_dev;         ID of device containing file 
     ino_t     st_ino;         inode number 
     mode_t    st_mode;        protection 
     nlink_t   st_nlink;       number of hard links 
     uid_t     st_uid;         user ID of owner 
     gid_t     st_gid;         group ID of owner 
     dev_t     st_rdev;        device ID (if special file) 
     off_t     st_size;        total size, in bytes 
     blksize_t st_blksize;     blocksize for filesystem I/O 
     blkcnt_t  st_blocks;      number of 512B blocks allocated 

     Since Linux 2.6, the kernel supports nanosecond precision for the following timestamp fields.
     For the details before Linux 2.6, see NOTES. 

     struct timespec st_atim;  time of last access 
     struct timespec st_mtim;  time of last modification 
     struct timespec st_ctim;  time of last status change 

     #define st_atime st_atim.tv_sec      Backward compatibility 
     #define st_mtime st_mtim.tv_sec
     #define st_ctime st_ctim.tv_sec
   };
*/


/*

III. Remote names

All names which start with "{" are remote names, and are in the form

	"{" remote_system_name [":" port] [flags] "}" [mailbox_name]

where:
 remote_system_name     Internet domain name or bracketed IP address of server.
 port                   optional TCP port number, default is the default port for that service		
 flags                  optional flags, one of the following:
  "/service=" service   mailbox access service, default is "imap"
  "/user=" user         remote user name for login on the server
  "/authuser=" user     remote authentication user; if specified this is the user name whose 
                        password is used (e.g. administrator)
  "/anonymous"          remote access as anonymous user
  "/debug"              record protocol telemetry in application's debug log
  "/secure"             do not transmit a plaintext password over the network
  "/imap", "/imap2", "/imap2bis", "/imap4", "/imap4rev1"
                        equivalent to /service=imap
  "/pop3"               equivalent to /service=pop3
  "/nntp"               equivalent to /service=nntp
  "/norsh"              do not use rsh or ssh to establish a preauthenticated
                        IMAP session
  "/ssl"                use the Secure Socket Layer to encrypt the session
  "/validate-cert"      validate certificates from TLS/SSL server (this is the default behavior)
  "/novalidate-cert"	do not validate certificates from TLS/SSL server, needed if server uses
                        self-signed certificates
  "/tls"                force use of start-TLS to encrypt the session, and reject connection
                        to servers that do not support it
  "/tls-sslv23"         use the depreciated SSLv23 client when negotiating TLS to the server.
                        This is necessary with some broken servers which (incorrectly) think that
                        TLS is just another way of doing SSL.
  "/notls"              do not do start-TLS to encrypt the session, even with servers that support it
  "/readonly"           request read-only mailbox open
                        (IMAP only; ignored on NNTP, and an error with SMTP and POP3)
  "/loser"              disable various protocol features and perform various client-side workarounds;
                        for example, it disables the SEARCH command in IMAP and does client-side
                        searching instead.  The precise measures taken by /loser depend upon the
                        protocol and are subject to change over time.  /loser is intended for use
                        with defective servers which do not implement the protocol specification
                        correctly.  It should be used only as a last resort since it will seriously
                        degrade performance.
 mailbox_name           remote mailbox name, default is INBOX

For example:

	{imap.foo.com}INBOX

opens an IMAP connection to system imap.foo.com and selects INBOX.

*/

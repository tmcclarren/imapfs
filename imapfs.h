#pragma once

#include <climits>
#include <memory>
#include <string>
#include <vector>
#include <set>

#define _FILE_OFFSET_BITS 64
#include <fuse.h>

#include <vmime/vmime.hpp>
#include <vmime/net/imap/imap.hpp>

std::ostream& operator << (std::ostream& os, const vmime::exception& e);

enum {
    E_HAVEMESSAGES = 1L << 0,
    E_NEEDSYNC = 1L << 1,
};

class NodeT;

extern char PATH_DELIMITER;
extern const std::string FS_PREFIX;

std::vector<std::string> split(const std::string& s, char delim);
NodeT* find(std::vector<std::string>::iterator at, const std::vector<std::string>::const_iterator& end, const NodeT& in);

struct NodeT {
    NodeT(const std::string& name, const std::string& uid):
        _name(name), _uid(uid), _flags(0L), _parent(NULL) {
        memset(&_stat, 0, sizeof(struct stat));
    }
    NodeT(const std::string& name): NodeT(name, "0") { }
    NodeT(): NodeT("") { }
    
    // operator overload so we can put them in a set
    bool operator < (const NodeT& rhs) const { return (_name < rhs._name); }
    
    const std::string& name() { return _name; }
    const std::string& uid() { return _uid; }
    
    std::string _name;
    // contents of below don't affect sort order, so they can be mutable
    std::string _uid;
    unsigned long _flags;
    struct stat _stat;
    mutable std::set<NodeT> _sub;

    NodeT* _parent;
    std::shared_ptr<vmime::net::folder> _folder;
    std::shared_ptr<vmime::net::message> _message;
    std::string _text;
    vmime::byteArray _contents;
};

class IMAPFS {
public:
    IMAPFS(const std::string& host, unsigned short port, const std::string& authuser, const std::string& password);

    int getattr(const std::string& path, struct stat* stat);
    int statfs(const std::string& path, struct statvfs* stat);
    int mknod(const std::string& path, mode_t mode);
    int readdir(const std::string& path, void* buf, fuse_fill_dir_t filler, off_t offset);
    int read(const std::string& path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int write(const std::string& path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int fsync(const std::string& path, int isdatasync, struct fuse_file_info* fi);
    int fallocate(const std::string& path, int mode, off_t offset, off_t length, struct fuse_file_info* fi);
    int truncate(const std::string& path, off_t size);
    int access(const std::string& path, int mask);
    int unlink(const std::string& path);
    int mkdir(const std::string& path, mode_t mode);
    int rmdir(const std::string& path);
    int release(const std::string& path, struct fuse_file_info* fi);
    int rename(const std::string& from, const std::string& to);

    std::shared_ptr<vmime::net::folder> openMailbox(const std::string& mailbox);
    std::shared_ptr<vmime::net::folder> createMailboxForPath(const std::string& path);

    int parseFilesystem();

    NodeT* findNode(const std::string& path);
    NodeT* findParent(const std::string& path);
    std::shared_ptr<vmime::net::folder> findFolder(const std::string& path);
    
    const std::string& host() { return _host; }
    
    std::string canonicalHost();

    void rebuildFolder(NodeT* in, std::shared_ptr<vmime::net::folder> folder);
    void rebuildFolders(NodeT* node, std::shared_ptr<vmime::net::folder> folder);
    void rebuildMessage(NodeT* in, std::shared_ptr<vmime::net::folder> folder, std::shared_ptr<vmime::net::message> message);
    void rebuildMessages(NodeT* node, std::shared_ptr<vmime::net::folder> folder);
    
    
    std::string _host;
    unsigned short _port;
    std::string _authuser;
    std::string _password;
    NodeT* _root;
    std::map<std::string, std::shared_ptr<vmime::net::folder>> _fsMap;
    std::shared_ptr<vmime::net::session> _session;
    std::shared_ptr<vmime::net::imap::IMAPStore> _store;
    char _seperator;
};

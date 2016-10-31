#pragma once

#include <climits>
#include <memory>
#include <string>
#include <vector>
#include <set>

#define _FILE_OFFSET_BITS 64
#include <fuse.h>

#include <vmime/vmime.hpp>

std::ostream& operator << (std::ostream& os, const vmime::exception& e);

class NodeT;

extern char PATH_DELIMITER;
extern const std::string FS_PREFIX;

std::vector<std::string> split(const std::string& s, char delim);
NodeT* find(std::vector<std::string>::iterator at, const std::vector<std::string>::const_iterator& end, const NodeT& in);

enum NodeFlags {
    E_HAVEFOLDERS = 1L << 0,
    E_HAVEMESSAGES = 1L << 1,
    E_MAILBOX = 1L << 62,
    E_MESSAGE = 1L << 63
};

struct MessageContent {
    std::shared_ptr<vmime::net::message> _message;
    std::string _text;
    std::string _buffer;
};

struct NodeT {
    NodeT(const std::string& name, unsigned long flags, unsigned long id, void* content):
        _name(name), _flags(flags), _id(id), _content(content), _parent(NULL) {
        memset(&_stat, 0, sizeof(struct stat));
    }
    NodeT(const std::string& name, unsigned long flags, unsigned long id): NodeT(name, flags, id, NULL) { }
    NodeT(const std::string& name): NodeT(name, 0L, 0L) { }
    NodeT(): NodeT("") { }
    
    // operator overload so we can put them in a set
    bool operator < (const NodeT& rhs) const { return (_name < rhs._name); }
    
    const std::string& name() { return _name; }
    const unsigned long id() { return _id; }
    const unsigned long flags() { return _flags; }
    
    std::string _name;
    // contents of below don't affect sort order, so they can be mutable
    mutable unsigned long _flags;
    mutable unsigned long _id;
    mutable void* _content;
    mutable struct stat _stat;
    mutable std::set<NodeT> _sub;
    NodeT* _parent;
    std::shared_ptr<vmime::net::folder> _folder;
};

class IMAPFS {
public:
    IMAPFS(const std::string& host, unsigned short port, const std::string& authuser, const std::string& password);
    IMAPFS(const std::string& host, const std::string& authuser, const std::string& password);

    std::shared_ptr<vmime::net::folder> openMailbox(const std::string& mailbox);

    int getattr(const std::string&, struct stat* stat);
    int statfs(const std::string& path, struct statvfs* stat);
    int mknod(const std::string& path, mode_t mode);
    int readdir(const std::string& path, void* buf, fuse_fill_dir_t filler, off_t offset);
    int read(const std::string& path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int write(const std::string& path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int fallocate(const std::string& path, int mode, off_t offset, off_t length, struct fuse_file_info* fi);
    int truncate(const std::string& path, off_t size);
    int fsync(const std::string& path, int isdatasync, struct fuse_file_info* fi);
    int access(const std::string& path, int mask);
    int unlink(const std::string& path);

    NodeT* find(const std::string& path, bool message = true);
    
    const std::string& host() { return _host; }
    
    std::string canonical_host();
    void rebuildSubfolders(NodeT* node, std::shared_ptr<vmime::net::folder> folder);
    void rebuildMessages(NodeT* node, std::shared_ptr<vmime::net::folder> folder);
    
    std::string _host;
    unsigned short _port;
    std::string _authuser;
    std::string _password;
    std::string _mailbox;
    NodeT _root;
    std::shared_ptr<vmime::net::folder> _current;
    std::shared_ptr<vmime::net::session> _session;
    std::shared_ptr<vmime::net::store> _store;
};

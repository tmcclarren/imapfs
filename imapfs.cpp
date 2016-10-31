#include <sstream>

#include "log.h"
#include "stack_trace.h"
#include "time.h"
#include "fs_log.h"
#include "imapfs.h"

using namespace std;
using namespace vmime;

char PATH_DELIMITER = '/';
const string FS_PREFIX = ".imapfs_";

set<string> _ignore { 
    PATH_DELIMITER + ".xdg-volume-info",
    PATH_DELIMITER + ".Trash",
    PATH_DELIMITER + ".Trash-1000",
    PATH_DELIMITER + "autorun.inf"
};

ostream& operator << (ostream& os, const vmime::exception& e)
{
    os << "vmime exception " << e.name() << endl;
    os << "    what = " << e.what() << endl;
    if (e.other()) {
        os << (*e.other());
    }
    return os;
}

static void dump(stringstream& ss, const NodeT& at, int indent)
{
    int tmp = indent;
    while (tmp--) {
        ss << " ";
    }
    ss << at._name << endl;
    for (set<NodeT>::iterator iter = at._sub.begin(); iter != at._sub.end(); ++iter) {
        dump(ss, *iter, indent + 4);
    }
}

string trim(const string& s)
{
    if (s[0] == PATH_DELIMITER) {
        return s.substr(1);
    }
    return s;
}

vector<string> split(const string& s, char delim)
{
    //LOG(LOG, INFO) << "splitting " << s;
    string tmp(s);
    if (tmp[0] == delim) {
        tmp = tmp.substr(1);
    }
    vector<string> elems;
    stringstream ss(tmp);
    string item;
    while (getline(ss, item, delim)) {
        elems.push_back(item);
    }
    ss.clear();
    for (vector<string>::iterator iter = elems.begin(); iter != elems.end(); ++iter) {
        ss << "/" << *iter;
    }
    //LOG(LOG, INFO) << "split " << ss.str();
    return elems;
}

NodeT* find(vector<string>::iterator at, const vector<string>::const_iterator& end, const NodeT& in)
{
    if (at == end) {
        return NULL;
    }
    
    set <NodeT>::iterator n = in._sub.find(*at);
    if (n != in._sub.end()) {
        vector<string>::iterator next = ++at;
        if (next == end) {
            // no more path elements to look at
            return const_cast<NodeT*>(&(*n));
        }
        return find(next, end, *n);
    }
    return NULL;
}

class _trace: public vmime::net::tracer
{
public:
    _trace(const vmime::string& proto, const int connectionID): _proto(proto), _connectionID(connectionID) { }
    
    void traceSend(const vmime::string& line) {
        LOG(LOG, INFO) << "[" << _proto << ":" << _connectionID << "] -> " << line << endl;
    }
    
    void traceReceive(const vmime::string& line) {
        LOG(LOG, INFO) << "[" << _proto << ":" << _connectionID << "] <- " << line << endl;
    }
    
    const vmime::string _proto;
    const int _connectionID;
};

class _tracefactory: public net::tracerFactory
{
public:
    shared_ptr<net::tracer> create(shared_ptr<net::service> serv, const int connectionID) {
        return make_shared<_trace>(serv->getProtocolName(), connectionID);
    }
};

class _timeouthandler: public net::timeoutHandler
{
public:
    _timeouthandler() {
        _startTime = time(NULL);
    }
    
    bool isTimeOut() {
        return (time(NULL) > _startTime + 4);
    }
    
    void resetTimeOut() {
        _startTime = time(NULL);
    }
    
    bool handleTimeOut() {
        cout << "timed out" << endl;
        return false;
    }
    
    time_t _startTime;
};

class _timeouthandlerfactory: public net::timeoutHandlerFactory
{
public:
    shared_ptr<net::timeoutHandler> create() {
        return make_shared<_timeouthandler>();
    }
};

class _certverify: public security::cert::certificateVerifier
{
public:
    void verify(shared_ptr<security::cert::certificateChain> chain, const string& hostname) {
    }
};

IMAPFS::IMAPFS(const string& host, unsigned short port, const string& authuser, const string& password):
    _host(host), _port(port), _authuser(authuser), _password(password), _mailbox(""), _root("", E_MAILBOX, 0)
{
    _session = net::session::create();
    propertySet& props = _session->getProperties();
    props["auth.username"] = _authuser;
    props["auth.password"] = _password;
    props["connection.tls.required"] = "false";
    string urlString = "imaps://" + _host;
    if (_port) {
        urlString += string(":" + to_string(_port));
    }
    utility::url url(urlString);

    url = "imaps://tim:v3rlYnu@localhost:2983";    
    _store = _session->getStore(url);
    _store->setTimeoutHandlerFactory(make_shared<_timeouthandlerfactory>());
    _store->setTracerFactory(make_shared<_tracefactory>());
    _store->setCertificateVerifier(make_shared<_certverify>());
    _store->connect();
    
    struct stat& rstat = _root._stat;
    rstat.st_nlink = 2;
    rstat.st_uid = fuse_get_context()->uid;
    rstat.st_gid = fuse_get_context()->gid;
    rstat.st_mode = S_IFDIR | 0755;
}

IMAPFS::IMAPFS(const string& host, const string& authuser, const string& password): IMAPFS(host, 0, authuser, password)
{
}

shared_ptr<net::folder> IMAPFS::openMailbox(const string& mailbox)
{
    net::folder::path path = utility::path::fromString(mailbox, "/", vmime::charset::getLocalCharset());
    
    string s = trim(mailbox);
    NodeT* n = &_root;
    if (mailbox != "") {
        n = find(s, false);
    }
    
    shared_ptr<net::folder> folder = n->_folder;
    if (!folder) {
        if (s == "") {
            folder = _store->getRootFolder();
        }
        else {
            folder = _store->getFolder(path);
        }
        if (!folder) {
            return NULL;
        }
        n->_folder = folder;
    }
    
    _current = folder;
    _mailbox = s;

    if (!(n->_flags & E_HAVEFOLDERS)) {
        rebuildSubfolders(n, folder);
        n->_flags |= (E_HAVEFOLDERS);
    }
    return _current;
}

int IMAPFS::getattr(const string& path, struct stat* status)
{
    LOGFN(LOG, INFO) << "getattr " << path;
    set<string>::iterator iter = _ignore.find(path);
    if (iter != _ignore.end()) {
        return -ENOENT;
    }
    
    if (path == "/") {
        memcpy(status, &(_root._stat), sizeof(struct stat));
        return 0;
    }
    
    NodeT* n = find(path, false);
    if (!n) {
        return -ENOENT;
    }

    LOGFN(LOG, INFO) << "stat for node " << n->_name;
    memcpy(status, &(n->_stat), sizeof(struct stat));
    return 0;
}

int IMAPFS::mknod(const string& path, mode_t mode)
{
    LOGFN(LOG, INFO) << "mknod " << path;
    
    vector<string> elems = split(path, PATH_DELIMITER);
    string leaf = elems.back(); elems.pop_back();
    NodeT n(leaf, E_MESSAGE, ULONG_MAX, new MessageContent());
    n._stat.st_nlink = 1;
    n._stat.st_mode = S_IFREG & mode;
    n._stat.st_uid = fuse_get_context()->uid;
    n._stat.st_gid = fuse_get_context()->gid;
    n._stat.st_size = n._stat.st_blksize = n._stat.st_blocks = 0;

    time_t t = Time().now().seconds();
    //LOG(LOG, INFO) << "yy/mm/dd " << tm.tm_year << "/" << (tm.tm_mon + 1) << "/" << tm.tm_mday
    //               << " hh:mm:ss " << tm.tm_hour << ":" << tm.tm_min << ":" << tm.tm_sec;
    n._stat.st_atim.tv_sec = n._stat.st_mtim.tv_sec = n._stat.st_ctim.tv_sec = t;

    NodeT* in = &_root;
    if (elems.size()) {
        // get correct mailbox, add node
        in = ::find(elems.begin(), elems.end(), _root);
        if (!in) {
            LOGFN(LOG, CRIT) << "can't find " << path;
            return -ENOENT;
        }
    }
    LOGFN(LOG, INFO) << "adding " << leaf << " to " << in->_name;
    n._parent = in;
    in->_sub.insert(n);

    //stringstream ss;
    //dump(ss, *in, 0);
    //LOGFN(LOG, DEBUG) << ss.str();
    return 0;
}

int IMAPFS::statfs(const string& path, struct statvfs* stat)
{ 
    LOGFN(LOG, INFO) << "statfs " << path;
    
    memset(stat, 0, sizeof(struct statvfs));
    stat->f_bsize = 4096;
    stat->f_frsize = 4096;
    stat->f_blocks = ULONG_MAX;
    stat->f_bfree = ULONG_MAX;
    stat->f_bavail = ULONG_MAX;
    stat->f_files = 0;
    stat->f_ffree = ULONG_MAX;
    stat->f_favail = ULONG_MAX;
    stat->f_fsid = 0xFEEDDEEF;
    stat->f_flag = ST_NODEV | ST_NOEXEC | ST_SYNCHRONOUS;
    stat->f_namemax = 255;
    return 0;
}
 
int IMAPFS::readdir(const string& path, void* buf, fuse_fill_dir_t filler, off_t offset)
{
    LOGFN(LOG, INFO) << "readdir " << path << " offset " << offset;
    
    int count = 0;
    
    if (path == "/") {
        this->openMailbox("");
        for (set<NodeT>::iterator iter = _root._sub.begin(); iter != _root._sub.end(); ++iter, ++count) {
            if (count < offset) {
                continue;
            }
            //LOGFN(LOG, DEBUG) << "adding " << iter->_name;
            int ret = filler(buf, iter->_name.c_str(), &(iter->_stat), count + 1);
            if (ret) {
                return 0;
            }
        }
    }
    else {
        LOGFN(LOG, INFO) << "opening " << path;
        shared_ptr<net::folder> folder = this->openMailbox(path);
        NodeT* n = find(path, false);
        if ((n->_flags & E_MAILBOX) && !(n->_flags & E_HAVEMESSAGES)) {
            net::folderAttributes attr = folder->getAttributes();
            int flags = attr.getFlags();
            int type = attr.getType();
            if (flags & net::folderAttributes::FLAG_NO_OPEN) {
                return 0;
            }
            if (!folder->isOpen()) {
                folder->open(net::folder::Modes::MODE_READ_WRITE);
            }
            if ( (type & net::folderAttributes::TYPE_CONTAINS_FOLDERS) &&
                 (flags & net::folderAttributes::FLAG_HAS_CHILDREN) ) {
            }
            else if (type & net::folderAttributes::TYPE_CONTAINS_MESSAGES) {
                vector<shared_ptr<net::message>> messages = folder->getMessages(net::messageSet::byNumber(1, -1));
                rebuildMessages(n, folder);
            }
            n->_flags |= (E_HAVEMESSAGES);
        }
        set<NodeT>& sub = n->_sub;
        for (set<NodeT>::iterator iter2 = sub.begin(); iter2 != sub.end(); ++iter2, ++count) {
            if (count < offset) {
                continue;
            }
            int ret = filler(buf, iter2->_name.c_str(), &(iter2->_stat), count + 1);
            if (ret) {
                return 0;
            }
        }
    }

    return 0;
}

int IMAPFS::read(const string& path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    (void) fi;
    LOGFN(LOG, INFO) << "read " << path << " at " << offset << ", " << size << " bytes";

    NodeT* n = find(path);
    if (!n) {
        LOGFN(LOG, CRIT) << "could not find " << path;
        return -1;
    }
    
    if (!n->_content) {
        LOGFN(LOG, CRIT) << "no content";
        return -1;
    }
    
    MessageContent* content = static_cast<MessageContent*>(n->_content);
    
    if (content->_text.size() != 0) {
        LOGFN(LOG, INFO) << "have cached message text: " << content->_text;
    }
    else if (content->_message) {
        _current->fetchMessage(content->_message,
            net::fetchAttributes(net::fetchAttributes::ENVELOPE | 
                                 net::fetchAttributes::STRUCTURE |
                                 net::fetchAttributes::CONTENT_INFO) );
        utility::outputStreamStringAdapter adapter(content->_text);
        content->_message->extract(adapter);
    }

    string dout = content->_text.substr(offset, size);
    memcpy(buf, &dout[0], dout.size());
    return dout.size();
}

int IMAPFS::write(const string& path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    (void) fi;
    LOGFN(LOG, INFO) << "write " << path << " at " << offset << ", " << size << " bytes";

    NodeT* n = find(path);
    if (!n) {
        return 0;
    }
    
    string writebuf(buf, size);
    MessageContent* content = static_cast<MessageContent*>(n->_content);
    string& buffer = content->_buffer;    
    if (static_cast<unsigned int>(offset) > buffer.size()) {
        buffer.resize(offset + size);
    }
    buffer.insert(buffer.end(), writebuf.begin(), writebuf.end());
    
    return size;
}

int IMAPFS::truncate(const string& path, off_t size)
{
    LOGFN(LOG, INFO) << "truncate " << path << " to " << size << " bytes";
    
    NodeT* n = find(path);
    if (!n) {
        return 0;
    }
    MessageContent* content = static_cast<MessageContent*>(n->_content);
    string& buffer = content->_buffer;    
    buffer.clear();
    buffer.resize(size);
    return fsync(path, 1, NULL);
}

int IMAPFS::fsync(const string& path, int isdatasync, struct fuse_file_info* fi)
{
    LOGFN(LOG, INFO) << "sync " << path;
    
    NodeT* n = find(path);
    if (!n) {
        return 0;
    }
    //MessageContent* content = static_cast<MessageContent*>(n->_content);
    //string& buffer = content->_buffer;
    
    return 0;
}

int IMAPFS::access(const string& path, int mask)
{
    LOGFN(LOG, INFO) "access " << path;
    NodeT* n = find(path);
    if ((mask & F_OK) && !n) {
        LOGFN(LOG, INFO) << "file does not exist";
        return -1;
    }
    return 0;
}

int IMAPFS::fallocate(const string& path, int mode, off_t offset, off_t length, struct fuse_file_info* fi)
{
    (void) mode;
    (void) fi;
    LOGFN(LOG, INFO) << "allocate for " << path << " " << length << " bytes, at " << offset;
    return 0;
}

int IMAPFS::unlink(const string& path)
{
    LOGFN(LOG, INFO) << "unlink " << path;
    NodeT* n = find(path);
    if (!n) {
        LOGFN(LOG, CRIT) << path << " not found";
        return -ENOENT;
    }
    n->_parent->_sub.erase(*n);
    return 0;
}

NodeT* IMAPFS::find(const string& path, bool message)
{
    string spath = trim(path);
    vector<string> elems = split(spath, PATH_DELIMITER);
    NodeT* n = ::find(elems.begin(), elems.end(), _root);
    if (!n) {
        //LOGFN(LOG, CRIT) << "path not found: " << spath;
        return NULL;
    }

    if (message && !(n->_flags & E_MESSAGE)) {
        //LOGFN(LOG, CRIT) << "path is not a message";
        return NULL;
    }
    return n;
}
    
string IMAPFS::canonical_host()
{
    stringstream ss;
    ss << "{" << _host;
    if (_port) {
        ss << ":" << _port;
    }
    return ss.str();
}

void IMAPFS::rebuildMessages(NodeT* in, shared_ptr<net::folder> folder)
{
    LOGFN(LOG, INFO) << "rebuildMessages for " << in->_name;
    in->_sub.clear();

    NodeT n;
    vector<shared_ptr<net::message>> messages =
        folder->getAndFetchMessages(net::messageSet::byNumber(1, -1),
                    net::fetchAttributes(net::fetchAttributes::FULL_HEADER |
                                         net::fetchAttributes::SIZE |
                                         net::fetchAttributes::UID) );
    for (vector<shared_ptr<net::message>>::iterator iter = messages.begin(); iter != messages.end(); ++iter) {
        shared_ptr<const header> header = (*iter)->getHeader();
        shared_ptr<const messageId> msgId = header->MessageId()->getValue<const messageId>();
        shared_ptr<const datetime> dateTime = header->Date()->getValue<const datetime>();
        
        MessageContent* content = new MessageContent();
        content->_message = *iter;
        //if (n._content) {
        //    delete (static_cast<MessageContent*>(n._content));
        //}
        n._content = content;
        const net::message::uid uid = (*iter)->getUID();
        string suid(uid);
        n._id = atol(suid.c_str());
        n._name = msgId->getId();        
        n._stat.st_nlink = 1;
        n._stat.st_mode = S_IFREG | 0644;
        n._stat.st_uid = fuse_get_context()->uid;
        n._stat.st_gid = fuse_get_context()->gid;
        n._stat.st_size = n._stat.st_blksize = n._stat.st_blocks = (*iter)->getSize();
        n._flags |= (E_MESSAGE);

        struct tm tm;
        tm.tm_sec = dateTime->getSecond();
        tm.tm_min = dateTime->getMinute();
        tm.tm_hour = dateTime->getHour();
        tm.tm_mday = dateTime->getDay();
        tm.tm_mon = dateTime->getMonth() - 1;
        tm.tm_year = dateTime->getYear() - 1970;
        time_t t = mktime(&tm);
        //LOG(LOG, INFO) << "yy/mm/dd " << tm.tm_year << "/" << (tm.tm_mon + 1) << "/" << tm.tm_mday
        //               << " hh:mm:ss " << tm.tm_hour << ":" << tm.tm_min << ":" << tm.tm_sec;
        n._stat.st_atim.tv_sec = n._stat.st_mtim.tv_sec = n._stat.st_ctim.tv_sec = t;
        n._parent = in;
        in->_sub.insert(n);
    }
}

void IMAPFS::rebuildSubfolders(NodeT* in, shared_ptr<net::folder> folder)
{
    LOGFN(LOG, INFO) << "rebuildSubfolders for " << in->_name;
    in->_sub.clear();

    NodeT n;
    vector<shared_ptr<net::folder>> folders = folder->getFolders();
    for (vector<shared_ptr<net::folder>>::iterator iter = folders.begin(); iter != folders.end(); ++iter) {
        n._name = (*iter)->getName().getBuffer();        
        n._stat.st_nlink = 2;
        n._stat.st_mode = S_IFDIR | 0755;
        n._stat.st_uid = fuse_get_context()->uid;
        n._stat.st_gid = fuse_get_context()->gid;
        n._stat.st_size = n._stat.st_blksize = n._stat.st_blocks = 4096;
        n._flags |= (E_MAILBOX);
        n._parent = in;
        in->_sub.insert(n);
    }
}

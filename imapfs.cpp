#include <sstream>
#include <algorithm>

#include <vmime/net/imap/IMAPUtils.hpp>

#include "log.h"
#include "stack_trace.h"
#include "time.h"
#include "fs_log.h"
#include "imapfs.h"

using namespace std;
using namespace vmime;

char PATH_DELIMITER = '/';
const string FS_PREFIX = ".fs";
const string FS_WARN = "DO NOT DELETE.  This is a generated message from IMAPFS.";
const string FS_BINSIZE_HEADER = "X-FS-Octets";

set<string> _ignore { 
    PATH_DELIMITER + "/.xdg-volume-info",
    PATH_DELIMITER + "/.Trash",
    PATH_DELIMITER + "/.Trash-1000",
    PATH_DELIMITER + "/autorun.inf"
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
    //ss.clear();
    //for (vector<string>::iterator iter = elems.begin(); iter != elems.end(); ++iter) {
    //    ss << "/" << *iter;
    //}
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
    _host(host), _port(port), _authuser(authuser), _password(password), _root(NULL)
{
    _session = net::session::create();
    string urlString = "imaps://";
    if (_authuser != "" && _password != "") {
        urlString += (_authuser + ":" + _password + "@");
    }
    urlString += _host;
    if (_port) {
        urlString += string(":" + to_string(_port));
    }
    utility::url url(urlString);

    _store = std::dynamic_pointer_cast<net::imap::IMAPStore>(_session->getStore(url));
    _store->setTimeoutHandlerFactory(make_shared<_timeouthandlerfactory>());
    _store->setTracerFactory(make_shared<_tracefactory>());
    _store->setCertificateVerifier(make_shared<_certverify>());
    _store->connect();
    // LAM
    _seperator = '/'; 
}

int IMAPFS::getattr(const string& path, struct stat* status)
{
    LOGFN(LOG, INFO) << "getattr " << path;
    set<string>::iterator iter = _ignore.find(path);
    if (iter != _ignore.end()) {
        return -ENOENT;
    }
    
    if (path == "/") {
        memcpy(status, &(_root->_stat), sizeof(struct stat));
        return 0;
    }
    
    NodeT* n = findNode(path);
    if (!n) {
        LOGFN(LOG, CRIT) << "path " << path << " not found";
        return -ENOENT;
    }

    LOGFN(LOG, INFO) << "stat for node " << n->_name;
    memcpy(status, &(n->_stat), sizeof(struct stat));
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

// mknod creates files within directories, in our case, only regular files, ever
int IMAPFS::mknod(const string& path, mode_t mode)
{
    LOGFN(LOG, INFO) << "mknod " << path << " mode " << mode;
    
    if (!(mode & S_IFREG)) {
        LOG(LOG, CRIT) << "not able to create this type of file";
        return -1;
    }
    
    NodeT* n = findNode(path);
    if (n) {
        LOG(LOG, CRIT) << "path already exists";
        return -EEXIST;
    }
    
    vector<string> elems = split(path, PATH_DELIMITER);
    string leaf = elems.back(); elems.pop_back();
    NodeT a(leaf, "0");
    a._stat.st_nlink = 1;
    a._stat.st_mode = mode;
    a._stat.st_uid = fuse_get_context()->uid;
    a._stat.st_gid = fuse_get_context()->gid;
    a._stat.st_size = a._stat.st_blksize = a._stat.st_blocks = 0;

    time_t t = Time().now().seconds();
    //LOG(LOG, INFO) << "yy/mm/dd " << tm.tm_year << "/" << (tm.tm_mon + 1) << "/" << tm.tm_mday
    //               << " hh:mm:ss " << tm.tm_hour << ":" << tm.tm_min << ":" << tm.tm_sec;
    a._stat.st_atim.tv_sec = a._stat.st_mtim.tv_sec = a._stat.st_ctim.tv_sec = t;

    NodeT* in = _root;
    if (elems.size()) {
        // get correct mailbox, add node
        in = ::find(elems.begin(), elems.end(), *_root);
        if (!in) {
            LOGFN(LOG, CRIT) << "can't find " << path;
            return -ENOENT;
        }
    }
    LOGFN(LOG, INFO) << "adding " << leaf << " to " << in->_name;
    a._parent = in;
    a._folder = in->_folder;
    in->_sub.insert(a);

    //stringstream ss;
    //dump(ss, *in, 0);
    //LOGFN(LOG, DEBUG) << ss.str();
    return 0;
}

int IMAPFS::readdir(const string& path, void* buf, fuse_fill_dir_t filler, off_t offset)
{
    LOGFN(LOG, INFO) << "readdir " << path << " offset " << offset;
    
    int count = 0;
    NodeT* n = findNode(path);
    if (!n) {
        return -ENOENT;
    }
    
    if (!(n->_flags & E_HAVEMESSAGES)) {
        shared_ptr<net::folder> folder = n->_folder;
        net::folderAttributes attr = folder->getAttributes();
        int flags = attr.getFlags();
        int type = attr.getType();
        if (flags & net::folderAttributes::FLAG_NO_OPEN) {
            LOGFN(LOG, CRIT) << "folder shouldn't have 'no open' attribute";
            return 0;
        }
        if (!(type & net::folderAttributes::TYPE_CONTAINS_MESSAGES)) {
            LOGFN(LOG, CRIT) << "folder should have 'TYPE_CONTAINS_MESSAGE' flag";
            return 0;
        }
        if (!folder->isOpen()) {
            folder->open(net::folder::Modes::MODE_READ_WRITE);
        }
        vector<shared_ptr<net::message>> messages = folder->getMessages(net::messageSet::byNumber(1, -1));
        rebuildMessages(n, folder);
        n->_flags |= (E_HAVEMESSAGES);
    }
    for (set<NodeT>::iterator iter = n->_sub.begin(); iter != n->_sub.end(); ++iter, ++count) {
        if (count < offset) {
            continue;
        }
        LOGFN(LOG, DEBUG) << "adding " << iter->_name;
        int ret = filler(buf, iter->_name.c_str(), &(iter->_stat), count + 1);
        if (ret) {
            return 0;
        }
    }
    return 0;
}

int IMAPFS::read(const string& path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    LOGFN(LOG, INFO) << "read " << path << " at " << offset << ", " << size << " bytes";

    NodeT* n = findNode(path);
    if (!n) {
        LOGFN(LOG, CRIT) << "could not find " << path;
        return -ENOENT;
    }
        
    shared_ptr<net::folder> folder = n->_folder;

    if (n->_contents.size() != 0) {
        LOGFN(LOG, INFO) << "have cached message contents";
    }
    else if (n->_message) {
        folder->fetchMessage(n->_message,
            net::fetchAttributes(net::fetchAttributes::ENVELOPE | 
                                 net::fetchAttributes::STRUCTURE |
                                 net::fetchAttributes::CONTENT_INFO) );
        shared_ptr<message> parsed = n->_message->getParsedMessage();
        std::vector<shared_ptr<const attachment>> attachments = attachmentHelper::findAttachmentsInMessage(parsed);
        if (attachments.size() != 1) {
            LOGFN(LOG, CRIT) << "expected one attachment";
            return -1;
        }
        shared_ptr<const attachment> fileAtt = attachments[0];
        shared_ptr<const contentHandler> data = fileAtt->getData();
        utility::outputStreamByteArrayAdapter badapter(n->_contents);
        data->extract(badapter);
    }
    else {
        LOG(LOG, CRIT) << "message empty";
        return -1;
    }
    
    if (n->_contents.size() < (offset + size)) {
        size = n->_contents.size() - offset;
    }
    memcpy(buf, &(n->_contents[offset]), size);
    LOGFN(LOG, INFO) << size << " bytes read";
    time_t t = Time().now().seconds();
    n->_stat.st_atim.tv_sec = t;
    return size;
}

int IMAPFS::write(const string& path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    LOGFN(LOG, INFO) << "write " << path << " at " << offset << ", " << size << " bytes";

    NodeT* n = findNode(path);
    if (!n) {
        return -ENOENT;
    }
    
    string writebuf(buf, size);
    byteArray& contents = n->_contents;    
    if (static_cast<unsigned int>(offset) > contents.size()) {
        contents.resize(offset + size);
    }
    contents.insert(contents.end(), writebuf.begin(), writebuf.end());

    n->_stat.st_size = n->_stat.st_blksize = n->_stat.st_blocks = contents.size();
    time_t t = Time().now().seconds();
    n->_stat.st_atim.tv_sec = n->_stat.st_mtim.tv_sec = t;
    n->_flags |= E_NEEDSYNC;
    return size;
}

int IMAPFS::fsync(const string& path, int isdatasync, struct fuse_file_info* fi)
{
    LOGFN(LOG, INFO) << "sync " << path;
    
    NodeT* n = findNode(path);
    if (!n) {
        return -ENOENT;
    }

    byteArray& contents = n->_contents;
   
    shared_ptr<net::folder> fsMailbox = n->_folder;
    
    vector<string> elems = split(path, '/');
    string filename = elems.back();
    
    messageBuilder mb;
    mb.setExpeditor(mailbox(_authuser + "@" + _host));
    addressList to;
    to.appendAddress(make_shared<mailbox>(_authuser + "@" + _host));
    mb.setRecipients(to);

    // the filename we want will get embedded in the subject here
    mb.setSubject(text(filename));

    mb.getTextPart()->setText(
        make_shared<stringContentHandler>(FS_WARN));

    word wname(filename);

    shared_ptr<utility::inputStreamByteBufferAdapter> adapter =
        make_shared<utility::inputStreamByteBufferAdapter>(&contents[0], contents.size());
    shared_ptr<contentHandler> ch = make_shared<streamContentHandler>(adapter, contents.size());
    
    shared_ptr<fileAttachment> fa = make_shared<fileAttachment>(ch, wname, vmime::mediaType());
    
    time_t t = Time().now().seconds();
    fa->getFileInfo().setCreationDate(datetime(t));
    
    mb.attach(fa);
    shared_ptr<message> msg = mb.construct();
    shared_ptr<header> header = msg->getHeader();
    shared_ptr<headerField> binsize = header->getField(FS_BINSIZE_HEADER);
    string ssize = to_string(contents.size());
    binsize->setValue(ssize);
    
    net::messageSet tmpAdd = fsMailbox->addMessage(msg);
    const net::UIDMessageRange tmpr = dynamic_cast<const net::UIDMessageRange&>(tmpAdd.getRangeAt(0));
    string newID = string(tmpr.getFirst());
    
    if (n->_uid != "0") {
        net::messageSet tmpDel = net::messageSet::byUID(net::message::uid(n->_uid));

        fsMailbox->deleteMessages(tmpDel);
        fsMailbox->expunge();
    }
    
    n->_uid = newID;
    n->_stat.st_size = n->_stat.st_blksize = n->_stat.st_blocks = contents.size();
    n->_stat.st_atim.tv_sec = n->_stat.st_mtim.tv_sec = t;

    vector<shared_ptr<net::message>> messages = fsMailbox->getMessages(net::messageSet::byUID(newID));
    n->_message = messages[0];
    
    return 0;
}

int IMAPFS::fallocate(const string& path, int mode, off_t offset, off_t length, struct fuse_file_info* fi)
{
    LOGFN(LOG, INFO) << "allocate for " << path << " " << length << " bytes, at " << offset;
    return 0;
}

int IMAPFS::truncate(const string& path, off_t size)
{
    LOGFN(LOG, INFO) << "truncate " << path << " to " << size << " bytes";
    
    NodeT* n = findNode(path);
    if (!n) {
        return -ENOENT;
    }
    return 0;
}

int IMAPFS::access(const string& path, int mask)
{
    LOGFN(LOG, INFO) "access " << path;
    NodeT* n = findNode(path);
    if ((mask & F_OK) && !n) {
        LOGFN(LOG, INFO) << "file does not exist";
        return -ENOENT;
    }
    return 0;
}

int IMAPFS::unlink(const string& path)
{
    LOGFN(LOG, INFO) << "unlink " << path;
    NodeT* n = findNode(path);
    if (!n) {
        LOGFN(LOG, CRIT) << path << " not found";
        return -ENOENT;
    }
    n->_parent->_sub.erase(*n);
    shared_ptr<net::folder> fsMailbox = n->_folder;
    if (n->_uid != "0") {
        net::messageSet tmpDel = net::messageSet::byUID(net::message::uid(n->_uid));

        fsMailbox->deleteMessages(tmpDel);
        fsMailbox->expunge();
    }
   return 0;
}

int IMAPFS::mkdir(const string& path, mode_t mode)
{
    LOGFN(LOG, INFO) << "mkdir path " << path;
    NodeT* n = findNode(path);
    if (n) {
        LOGFN(LOG, CRIT) << "path already exists";
        return -EEXIST;
    }
    
    n = findParent(path);
    if (!n) {
        LOGFN(LOG, CRIT) << "no parent for " << path;
        return -ENOENT;
    }
    
    shared_ptr<net::folder> mailbox = createMailboxForPath(path);
    this->rebuildFolder(n, mailbox);
  
    return 0;
}

int IMAPFS::rmdir(const string& path)
{
    LOGFN(LOG, INFO) << "rmdir path " << path;
    return 0;
}

int IMAPFS::release(const string& path, struct fuse_file_info* fi)
{
    LOGFN(LOG, INFO) << "release " << path;

    NodeT* n = findNode(path);
    if (!n) {
        LOGFN(LOG, CRIT) << path << " not found";
        return -ENOENT;
    }
    if (n->_flags & E_NEEDSYNC) {
        int err = fsync(path, 1, NULL);
        if (!err) {
            n->_flags &= ~E_NEEDSYNC;
        }
        else {
            return err;
        }
    }
    n->_contents.resize(0);
    n->_flags = 0;
    return 0;
}

int IMAPFS::rename(const std::string& from, const std::string& to)
{
    return 0;
}


shared_ptr<net::folder> IMAPFS::openMailbox(const string& mailbox)
{
    net::folder::path path = utility::path::fromString(mailbox, "/", vmime::charset::getLocalCharset());
    
    string s = trim(mailbox);
    NodeT* n = _root;
    if (s != "") {
        n = findNode(s);
    }
    
    shared_ptr<net::folder> folder = n->_folder;
    if (!folder) {
        if (s == "/") {
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
    
    return folder;
}

shared_ptr<net::folder> IMAPFS::createMailboxForPath(const string& path)
{
    // LAM check to see if "path" exists in _fsMap
    map<string, shared_ptr<net::folder>>::iterator iter = _fsMap.find(path);
    if (iter != _fsMap.end()) {
        LOGFN(LOG, CRIT) << "path " << path << " already exists";
        return nullptr;
    }
    
    string mboxName = FS_PREFIX + path;
    replace(mboxName.begin(), mboxName.end(), '/', ':'); 
    if (mboxName.length() > 250) {
        LOGFN(LOG, CRIT) << "mailbox name length too long: " << path;
        return nullptr;
    }
    
    // mboxName will be a top-level mailbox, with a name that encodes the
    // path seperator as ':' instead of '/', mpath here is just a different
    // type to satisfy the nutty VMIME API that overly complicates things
    // by using it's own types everywhere
    net::folder::path mpath = net::imap::IMAPUtils::stringToPath(_seperator, mboxName);
    shared_ptr<net::folder> fsMailbox = _store->getFolder(mpath);
    net::folderAttributes attr;
    attr.setType(net::folderAttributes::TYPE_CONTAINS_MESSAGES);
    fsMailbox->create(attr);
    fsMailbox->open(net::folder::MODE_READ_WRITE);
    messageBuilder mb;
    mb.setExpeditor(mailbox(_authuser + "@" + _host));
    addressList to;
    to.appendAddress(make_shared<mailbox>(_authuser + "@" + _host));
    mb.setRecipients(to);
    mb.setSubject(text(path));
    mb.getTextPart()->setText(
        make_shared<stringContentHandler>(FS_WARN));
    shared_ptr<message> msg = mb.construct();
    fsMailbox->addMessage(msg);
    _fsMap.insert(pair<string, shared_ptr<net::folder>>(mboxName, fsMailbox));
    return fsMailbox;
}

int IMAPFS::parseFilesystem()
{
    _fsMap.clear();
    
    if (_root) {
        delete _root;
    }
    _root = new NodeT("/");
    _root->_stat.st_nlink = 2;
    _root->_stat.st_mode = S_IFDIR | 0755;
    _root->_stat.st_uid = getuid();
    _root->_stat.st_gid = getgid();
    _root->_stat.st_size = _root->_stat.st_blksize = _root->_stat.st_blocks = 4096;
    _root->_parent = NULL;

    shared_ptr<net::folder> root = _store->getRootFolder();
    vector<shared_ptr<net::folder>> folders = root->getFolders();
    
    for (vector<shared_ptr<net::folder>>::iterator iter = folders.begin(); iter != folders.end(); ++iter) {
        shared_ptr<net::folder> folder = *iter;
        string folderName = folder->getName().getBuffer();
        // skip everything that doesn't start with ".fs"
        if (folderName.substr(0, FS_PREFIX.length()) != FS_PREFIX) {
            continue;
        }
        
        folder->open(net::folder::MODE_READ_WRITE);
        shared_ptr<net::message> message = folder->getMessage(1);
        folder->fetchMessage(message, net::fetchAttributes::ENVELOPE);
        shared_ptr<const headerField> sfield = message->getHeader()->Subject();
        shared_ptr<const text> svalue = sfield->getValue<const text>();
        const string f = svalue->getWholeBuffer();
        string path(f);
        LOGFN(LOG, INFO) << "filesystem has path " << path;
        _fsMap.insert(pair<string, shared_ptr<net::folder>>(path, folder));
    }

    if (_fsMap.size() == 0) {
        LOGFN(LOG, INFO) << "no root filesystem, creating";
        _root->_folder = createMailboxForPath("/");
    }
    else {
        _root->_folder = findFolder("/");
    }
    
    for (map<string, shared_ptr<net::folder>>::iterator iter = _fsMap.begin(); iter != _fsMap.end(); ++iter) {
        string path = iter->first;
        LOGFN(LOG, INFO) << "find parent for " << path;
        NodeT* n = findParent(path);
        if (n) {
            this->rebuildFolder(n, iter->second);
        }
        n = findNode(path);
        if (!n) {
            LOGFN(LOG, CRIT) << "failed to add node " << path;
            continue;
        }
        n->_folder = iter->second;
    }
    
    return 0;
}

NodeT* IMAPFS::findParent(const string& path)
{
    if (!_root) {
        return NULL;
    }
    if (path == "/") {
        return NULL;
    }
  
    NodeT* n = _root;
    vector<string> elems = split(path, '/');
    elems.pop_back();
    if (elems.size()) {
        n = ::find(elems.begin(), elems.end(), *_root);
    }
    return n;    
}

NodeT* IMAPFS::findNode(const string& path)
{
    if (!_root) {
        return NULL;
    }
    if (path == "/") {
        return _root;
    }
    vector<string> elems = split(path, PATH_DELIMITER);
    NodeT* n = ::find(elems.begin(), elems.end(), *_root);
    if (!n) {
        //LOGFN(LOG, CRIT) << "path not found: " << spath;
        return NULL;
    }
    return n;
}

shared_ptr<net::folder> IMAPFS::findFolder(const string& path)
{
    LOGFN(LOG, INFO) << "findFolder " << path;
    shared_ptr<net::folder> ret;
    map<string, shared_ptr<net::folder>>::iterator iter = _fsMap.find(path);
    if (iter != _fsMap.end()) {
        ret = iter->second;
    }
    else {
        LOGFN(LOG, INFO) << "no path found";
    }
    return ret;
}

string IMAPFS::canonicalHost()
{
    stringstream ss;
    ss << "{" << _host;
    if (_port) {
        ss << ":" << _port;
    }
    return ss.str();
}

void IMAPFS::rebuildMessage(NodeT* in, shared_ptr<net::folder> folder, shared_ptr<net::message> message)
{
    shared_ptr<const header> header = message->getHeader();
    shared_ptr<const datetime> dateTime = header->Date()->getValue<const datetime>();
    shared_ptr<const text> filename = header->Subject()->getValue<const text>();
    shared_ptr<const text> tbinsize = header->findField(FS_BINSIZE_HEADER)->getValue<const text>();
    
    NodeT n;

    n._message = message;
    
    const net::message::uid uid = message->getUID();
    n._uid = string(uid);
    n._name = filename->getWholeBuffer();
    n._name = trim(n._name);
    n._stat.st_nlink = 1;
    n._stat.st_mode = S_IFREG | 0644;
    n._stat.st_uid = fuse_get_context()->uid;
    n._stat.st_gid = fuse_get_context()->gid;
    string ssize = tbinsize->getWholeBuffer();
    n._stat.st_size = n._stat.st_blksize = n._stat.st_blocks = atol(ssize.c_str());;
    struct tm tm;
    tm.tm_sec = dateTime->getSecond();
    tm.tm_min = dateTime->getMinute();
    tm.tm_hour = dateTime->getHour();
    tm.tm_mday = dateTime->getDay();
    tm.tm_mon = dateTime->getMonth() - 1;
    tm.tm_year = dateTime->getYear() - 1900;
    time_t t = mktime(&tm);
    //LOG(LOG, INFO) << "yy/mm/dd " << tm.tm_year << "/" << (tm.tm_mon + 1) << "/" << tm.tm_mday
    //               << " hh:mm:ss " << tm.tm_hour << ":" << tm.tm_min << ":" << tm.tm_sec;
    n._stat.st_atim.tv_sec = n._stat.st_mtim.tv_sec = n._stat.st_ctim.tv_sec = t;

    n._parent = in;
    n._folder = folder;
    in->_sub.insert(n);
}

void IMAPFS::rebuildMessages(NodeT* in, shared_ptr<net::folder> folder)
{
    LOGFN(LOG, INFO) << "rebuildMessages for " << in->_name;
    //in->_sub.clear();

    if (folder->getMessageCount() <= 1) {
        return;
    }
    
    // msg '1' is meta data regarding which folder this is... not an actual
    // filesystem node
    vector<shared_ptr<net::message>> messages =
        folder->getAndFetchMessages(net::messageSet::byNumber(2, -1),
                    net::fetchAttributes(net::fetchAttributes::FULL_HEADER |
                                         net::fetchAttributes::SIZE |
                                         net::fetchAttributes::UID) );
    for (vector<shared_ptr<net::message>>::iterator iter = messages.begin(); iter != messages.end(); ++iter) {
        rebuildMessage(in, folder, *iter);
    }
}

void IMAPFS::rebuildFolder(NodeT* in, shared_ptr<net::folder> folder)
{
    if (!in) {
        LOGFN(LOG, CRIT) << "parent node can't be null";
        return;
    }
    
    NodeT n;
    string mboxName(folder->getName().getBuffer());
    n._name = mboxName.substr(FS_PREFIX.length() + 1);
    n._stat.st_nlink = 2;
    n._stat.st_mode = S_IFDIR | 0755;
    n._stat.st_uid = getuid();
    n._stat.st_gid = getgid();
    n._stat.st_size = n._stat.st_blksize = n._stat.st_blocks = 4096;
    n._parent = in;

    // LAM not sure what timestamps to put on a newly mounted FS... go with "now"
    time_t t = Time().now().seconds();
    n._stat.st_atim.tv_sec = n._stat.st_mtim.tv_sec = n._stat.st_ctim.tv_sec = t;

    in->_sub.insert(n);
}

void IMAPFS::rebuildFolders(NodeT* in, shared_ptr<net::folder> folder)
{
    LOGFN(LOG, INFO) << "rebuildFolders for " << in->_name;

    vector<shared_ptr<net::folder>> folders = folder->getFolders();
    for (vector<shared_ptr<net::folder>>::iterator iter = folders.begin(); iter != folders.end(); ++iter) {
        rebuildFolder(in, *iter);
    }
}

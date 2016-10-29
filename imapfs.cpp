
#include <sstream>
#include <vmime/vmime.hpp>

#include "log.h"
#include "time.h"
#include "fs_log.h"
#include "imapfs.h"

using namespace std;

char PATH_DELIMITER = '/';
const string FS_MAILBOXROOTNAME = ".imapfs_root";

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


IMAPFS::IMAPFS(const string& host, unsigned short port, const vector<string>& args):
    _host(host), _port(port), _args(args), _mailbox(""), _root("", E_MAILBOX, 0)
{
}

IMAPFS::IMAPFS(const string& host, const vector<string>& args): IMAPFS(host, 0, args)
{
}

IMAPFS::IMAPFS(const string& host): IMAPFS(host, 0, vector<string> { })
{
}
IMAPFS::IMAPFS(): IMAPFS("")
{
}

#if 0
MAILSTREAM* IMAPFS::open(const string& mailbox)
{
    static bool first = true;
    MAILSTREAM* ms = NULL;

    string spath(mailbox);
    if (spath[0] == PATH_DELIMITER) {
        spath = spath.substr(1);
    }
    if (first) {
//        ms = mail_open(_mstream, const_cast<char*>(canonical_host().c_str()), OP_DEBUG);
    }
    else {
//        ms = mail_open(_mstream, const_cast<char*>((canonical_host() + spath).c_str()), OP_DEBUG);
    }
    if (!ms) {
        return NULL;
    }
    _mstream = ms;
    _mailbox = mailbox;
    //mail_debug(_mstream);
    _mstream->sparep = this;
    if (first) {
        first = false;
        rebuildRoot();
    }
    return _mstream;
}
#endif

int IMAPFS::getattr(const string& path, struct stat* status)
{
    //LOG(LOG, INFO) << "getattr " << path;
 
    set<string>::iterator iter = _ignore.find(path);
    if (iter != _ignore.end()) {
        return -ENOENT;
    }
    
    status->st_nlink = 1;
    status->st_uid = fuse_get_context()->uid;
    status->st_gid = fuse_get_context()->gid;
    status->st_mode = S_IFREG | 0644;

    if (path == "/") {
        //LOG(LOG, INFO) << "root";
        status->st_nlink = 2;
        status->st_mode = S_IFDIR | 0755;
        return 0;
    }
    
    NodeT* n = find(path, false);
    if (!n) {
        //stringstream ss;
        //dump(ss, _fs->_root, 0);
        //LOG(LOG, DEBUG) << endl << ss.str();
        return -ENOENT;
    }
    //LOGFN(LOG, INFO) << "stat for node " << n->_name;
    memcpy(status, &(n->_stat), sizeof(struct stat));
    return 0;
}

int IMAPFS::mknod(const string& path, mode_t mode)
{
    LOGFN(LOG, INFO) << "mknod " << path;
    string mailbox(FS_MAILBOXROOTNAME);
    
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

    NodeT* in = NULL;
    if (elems.size()) {
        // get correct mailbox, add node
        in = ::find(elems.begin(), elems.end(), _root);
        if (!in) {
            LOGFN(LOG, CRIT) << "can't find " << path;
            return -ENOENT;
        }
        LOGFN(LOG, INFO) << "adding " << leaf << " to " << in->_name;
        in->_sub.insert(n);
    }
    else {
        // add node to "root" mailbox
        LOGFN(LOG, INFO) << "adding " << leaf << " to root";
        in = &_root;
        in->_sub.insert(n);        
    }
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
 
 #if 0
 void overview_cb(MAILSTREAM* ms, unsigned long uid, OVERVIEW* ov, unsigned long msgno)
{
    static string _savedmailbox("");
    static NodeT* _savednode = NULL;

    IMAPFS* fs = static_cast<IMAPFS*>(ms->sparep);
    string mailbox(fs->_mailbox);
    
    //LOG(LOG, INFO) << "overview cb, mailbox is " << mailbox;
    
    if (mailbox != _savedmailbox) {
        vector<string> elems = split(mailbox, PATH_DELIMITER);
        _savednode = find(elems.begin(), elems.end(), fs->_root);
        if (!_savednode) {
            LOGFN(LOG, CRIT) << "problem finding " << mailbox;
            return;
        }
        _savedmailbox = mailbox;
        //LOG(LOG, INFO) << "saved mailbox is " << _savednode->_name;
    }
    
    string name("[no name]");
    if (ov->message_id) {
        name = ov->message_id;
    }
    else if (uid) {
        name = to_string(uid);
    }
    
    NodeT n(name, E_MESSAGE, uid, new MessageContent());
    n._stat.st_nlink = 1;
    n._stat.st_mode = S_IFREG | 0644;
    n._stat.st_uid = fuse_get_context()->uid;
    n._stat.st_gid = fuse_get_context()->gid;
    n._stat.st_size = n._stat.st_blksize = n._stat.st_blocks = ov->optional.octets;

    MESSAGECACHE elt;
    long res = mail_parse_date(&elt, (unsigned char*)(ov->date));
    if (res == T) {
        struct tm tm;
        tm.tm_sec = elt.seconds;
        tm.tm_min = elt.minutes;
        tm.tm_hour = elt.hours;
        tm.tm_mday = elt.day;
        tm.tm_mon = elt.month - 1;
        tm.tm_year = elt.year + BASEYEAR - 1900;
        time_t t = mktime(&tm);
        //LOG(LOG, INFO) << "yy/mm/dd " << tm.tm_year << "/" << (tm.tm_mon + 1) << "/" << tm.tm_mday
        //               << " hh:mm:ss " << tm.tm_hour << ":" << tm.tm_min << ":" << tm.tm_sec;
        n._stat.st_atim.tv_sec = n._stat.st_mtim.tv_sec = n._stat.st_ctim.tv_sec = t;
    }

    set<NodeT>& sub = _savednode->_sub;
    sub.insert(n);
}
#endif

int IMAPFS::readdir(const string& path, void* buf, fuse_fill_dir_t filler, off_t offset)
{
    //LOGFN(LOG, INFO) << "readdir " << path;
    
    int count = 0;
    
//    if (!_mstream) {
//        open("");
//    }
    
    if (path == "/") {
        for (set<NodeT>::iterator iter = _root._sub.begin(); iter != _root._sub.end(); ++iter, ++count) {
            if (count < offset) {
                continue;
            }
            //LOG(LOG, DEBUG) << "adding " << iter->_name;
            filler(buf, iter->_name.c_str(), &(iter->_stat), 0);
        }
    }
    else {
        //LOG(LOG, INFO) << "opening " << spath;
//        open(path);
//        mail_fetch_overview_sequence(_mstream, (char*)("1:*"), overview_cb);
        NodeT* n = find(path, false);
        if (!n) {
            return -1;
        }
        
        set<NodeT>& sub = n->_sub;
        for (set<NodeT>::iterator iter2 = sub.begin(); iter2 != sub.end(); ++iter2, ++count) {
            if (count < offset) {
                continue;
            }
            filler(buf, iter2->_name.c_str(), &(iter2->_stat), 0);
        }
    }

    return 0;
}

int IMAPFS::read(const string& path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    (void) offset;
    (void) fi;
    LOGFN(LOG, INFO) << "read " << path << " at " << offset << ", " << size << " bytes";

    NodeT* n = find(path);
    if (!n) {
        return 0;
    }
    
#if 0
    MessageContent* content = static_cast<MessageContent*>(n->_content);
    unsigned long msgno = mail_msgno(_mstream, n->_id);
    LOGFN(LOG, INFO) << "fetching message " << msgno;
    if (!content->_envelope) {
        content->_envelope = mail_fetch_structure(_mstream, msgno, &(content->_body), 0);
        if (!content->_envelope) {
            LOGFN(LOG, CRIT) << "no envelope";
        }
        if (!content->_body) {
            LOGFN(LOG, CRIT) << "no body";
        }
    }
    else {
        LOGFN(LOG, INFO) << "have cached envelope and body";
    }

    if (!content->_text) {
        unsigned long len = 0;
        content->_text = mail_fetch_message(_mstream, msgno, &len, 0);
        if (!content->_text) {
            LOGFN(LOG, CRIT) << "no text";
        }
    }
    else {
        LOGFN(LOG, INFO) << "have cached message text";
    }

    //LOG(LOG, INFO) << "output message " << msgno;
    string out = string(content->_text);
    string dout = out.substr(offset, size);
    memcpy(buf, &dout[0], size);
    return dout.length();
#endif
    return 0;
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

int IMAPFS::truncate(const std::string& path, off_t size)
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

int IMAPFS::fsync(const std::string& path, int isdatasync, struct fuse_file_info* fi)
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

NodeT* IMAPFS::find(const std::string& path, bool message)
{
    string spath(path);
    if (spath[0] == PATH_DELIMITER) {
        spath = spath.substr(1);
    }
    vector<string> elems = split(spath, PATH_DELIMITER);
    NodeT* n = ::find(elems.begin(), elems.end(), _root);
    if (!n) {
        LOGFN(LOG, CRIT) << "path not found";
        return NULL;
    }

    if (message && !(n->_flags & E_MESSAGE)) {
        LOGFN(LOG, CRIT) << "path is not a message";
        return NULL;
    }
    return n;
}
    
int IMAPFS::fallocate(const string& path, int mode, off_t offset, off_t length, struct fuse_file_info* fi)
{
    (void) mode;
    (void) fi;
    LOGFN(LOG, INFO) << "allocate for " << path << " " << length << " bytes, at " << offset;
    return 0;
}


string IMAPFS::canonical_host()
{
    stringstream ss;
    ss << "{" << _host;
    if (_port) {
        ss << ":" << _port;
    }
    for (vector<string>::iterator iter = _args.begin(); iter != _args.end(); ++iter) {
        ss << "/";
        ss << *iter;
    }
    ss << "}";
    return ss.str();
}

void IMAPFS::rebuildRoot()
{
    _root._sub.clear();
    string all("{" + host() + "}*");
//    mail_list(_mstream, NULL, const_cast<char*>(all.c_str()));
}
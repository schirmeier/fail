#include <string>
#include <errno.h>

#include "SocketComm.hpp"

namespace fail {

bool SocketComm::sendMsg(int sockfd, google::protobuf::Message& msg)
{
#ifdef USE_SIZE_PREFIX
    int size = htonl(msg.ByteSize());
    std::string buf;
    if (safe_write(sockfd, &size, sizeof(size)) == -1
	 || !msg.SerializeToString(&buf)
	 || safe_write(sockfd, buf.c_str(), buf.size()) == -1) {
        return false;
    }
#else
    char c = 0;
    if (!msg.SerializeToFileDescriptor(sockfd)
	 || safe_write(sockfd, &c, 1) == -1) {
        return false;
    }
#endif
    return true;
}
  
bool SocketComm::rcvMsg(int sockfd, google::protobuf::Message& msg)
{
#ifdef USE_SIZE_PREFIX
    int size;
    if (safe_read(sockfd, &size, sizeof(size)) == -1) {
        return false;
    }
    size = ntohl(size);
    char *buf = new char[size];
    if (safe_read(sockfd, buf, size) == -1) {
        delete [] buf;
        return false;
    }
    std::string st(buf, size);
    delete [] buf;
    return msg.ParseFromString(st);
#else
    return msg.ParseFromFileDescriptor(sockfd);
#endif
}

ssize_t SocketComm::safe_write(int fd, const void *buf, size_t count)
{
	ssize_t ret;
	const char *cbuf = (const char *) buf;
	do {
		ret = write(fd, cbuf, count);
		if (ret == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		count -= ret;
		cbuf += ret;
	} while (count);
	return cbuf - (const char *)buf;
}

ssize_t SocketComm::safe_read(int fd, void *buf, size_t count)
{
	ssize_t ret;
	char *cbuf = (char *) buf;
	do {
		ret = read(fd, cbuf, count);
		if (ret == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		} else if (ret == 0) {
			// this deliberately deviates from read(2)
			return -1;
		}
		count -= ret;
		cbuf += ret;
	} while (count);
	return cbuf - (const char *) buf;
}

} // end-of-namespace: fail
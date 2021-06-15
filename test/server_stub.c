/**
 * @brief A server stub for testing basic client-server API functionalities.
 */
#include <defines.h>
#include <util.h>
#include <protocol.h>

#define SOCKNAME "socketStub.sk"

/* Example of expelled file */
#define EXPEL_PATH "/dir1/dir2/stubfile.txt"
#define EXPEL_PSIZE strlen(EXPEL_PATH)+1
#define EXPEL_DATA "123456789012345678901234567890abcdef"
#define EXPEL_DSIZE strlen(EXPEL_DATA)+1

void cleanup(void){
	unlink(SOCKNAME);
}

int main(void){
	atexit(cleanup);
	int s;
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
	SYSCALL_EXIT( (s = socket(AF_UNIX, SOCK_STREAM, 0)), "socket");
	SYSCALL_EXIT( bind(s, (const struct sockaddr*)&sa, sizeof(sa)), "bind");
	SYSCALL_EXIT( listen(s, SOMAXCONN), "listen");
	int nfd;
	SYSCALL_EXIT( (nfd = accept(s, NULL, NULL)), "accept");
	message_t* msg;
	msg_t mtype;
	while (true){
		SYSCALL_EXIT(mrecv(nfd, &msg, "mrecv - 1", "mrecv - 2"), "mrecv");
		mtype = msg->type;
		msg_destroy(msg, free, free);
		switch(mtype){
			case M_WRITEF:
			case M_APPENDF:
			{
				bool modified = true;
				SYSCALL_EXIT(msend(nfd, &msg, M_GETF, "msend - 1", "msend - 2", EXPEL_PSIZE, EXPEL_PATH, EXPEL_DSIZE, EXPEL_DATA, sizeof(bool), &modified) , "msend");
				SYSCALL_EXIT(msend(nfd, &msg, M_OK, "msend - 1", "msend - 2"), "msend");
				printf("Write/append operation done\n");
				break;
			}
			case M_READF:
			case M_READNF:
			{
				SYSCALL_EXIT(msend(nfd, &msg, M_ERR, "msend - 1", "msend - 2", sizeof(errno), ENOENT), "msend");
				printf("Reading operation done\n");
				break;
			}
			default:
			{
				SYSCALL_EXIT(msend(nfd, &msg, M_OK, "msend - 1", "msend - 2"), "msend");
				printf("Other operation done\n");
				break;			
			}
		}
	}
	unlink(SOCKNAME);
	/* Never arrive here */
	return 0;
}

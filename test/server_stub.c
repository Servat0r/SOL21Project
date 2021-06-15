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

/* Example of read file */
#define READF_DATA "abcdefghijklmnopqrstuvwxyz"
#define READF_DSIZE strlen(READF_DATA)+1


void cleanup(void){	unlink(SOCKNAME); }


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
	char* pathname;
	while (true){
		pathname = NULL;
		SYSCALL_EXIT(mrecv(nfd, &msg, "mrecv - 1", "mrecv - 2"), "mrecv");
		mtype = msg->type;
		if ((mtype == M_READF)){
			pathname = malloc(msg->args[0].len);
			memset(pathname, 0, msg->args[0].len);
			strncpy(pathname, (char*)msg->args[0].content, msg->args[0].len);
		}
		msg_destroy(msg, free, free);
		if (pathname){ DBGSTR(pathname); }
		else printf("pathname is NULL\n");
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
			{
				/* When asking to read a file, it behaves like that file was in memory with content as defined in READF_DATA */
				bool modified = false;
				if (pathname){
					SYSCALL_EXIT(msend(nfd, &msg, M_GETF, "msend - 1", "msend - 2", strlen(pathname)+1, pathname, READF_DSIZE, READF_DATA, sizeof(bool), &modified), "msend");
					SYSCALL_EXIT(msend(nfd, &msg, M_OK, "msend - 1", "msend - 2"), "msend");
					printf("Reading operation done\n");
					free(pathname);
					break;
				}
				/* If pathname is NULL, then an error message shall be sent */
			}
			case M_READNF:
			{
				int err_no_ent = ENOENT;
				SYSCALL_EXIT(msend(nfd, &msg, M_ERR, "msend - 1", "msend - 2", sizeof(errno), &err_no_ent), "msend");
				printf("Reading N files operation done\n");
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

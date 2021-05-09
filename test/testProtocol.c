#include <defines.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <protocol.h>
#include <assert.h>



static int error = 2; /* Example of variable for errno */

int main(void){

	int pfd[2];
	pipe(pfd);
	switch(fork()){
		case -1: exit(EXIT_FAILURE);

		case 0: {
			message_t* msg;
			close(pfd[1]);

			msg = msg_init();
			msg_recv(msg, pfd[0]);
			printf("Message received: \n");
			printMsg(msg);
			printf("Argument = %d\n", *((int*)msg->args[0].content));
			msg_destroy(msg, free, free);

			msg = msg_init();
			msg_recv(msg, pfd[0]);
			printf("Message received: \n");
			printMsg(msg);
			printf("Argument = %s\n", (char*)msg->args[0].content);
			msg_destroy(msg, free, free);

			msg = msg_init();
			msg_recv(msg, pfd[0]);
			printf("Message received: \n");
			printMsg(msg);
			printf("Arguments = %s, %s\n", (char*)msg->args[0].content, (char*)msg->args[1].content);
			msg_destroy(msg, free, free);

			msg = msg_init();
			msg_recv(msg, pfd[0]);
			printf("Message received: \n");
			printMsg(msg);
			printf("Argument = %d\n", *((int*)msg->args[0].content));
			msg_destroy(msg, free, free);

			msg = msg_init();
			msg_recv(msg, pfd[0]);
			printf("Message received: \n");
			printMsg(msg);
			printf("Argument = %s\n", (char*)msg->args[0].content);
			msg_destroy(msg, free, free);

			close(pfd[0]);
			return 0;
		}

		default: {
			message_t* msg;
			close(pfd[0]);
			int wstatus;
			int flags = 32;
			msg = msg_init();
			packet_t* p = packet_openf(&flags);
			assert(p);
			msg_make(msg, M_OPENF, "home1", p);
			SYSCALL_EXIT(msg_send(msg, pfd[1]), "When sending message for openf\n");
			msg_destroy(msg, NULL, NULL);
			
			msg = msg_init();
			p = packet_writef("pippo");
			assert(p);
			msg_make(msg, M_WRITEF, "home2", p);
			SYSCALL_EXIT(msg_send(msg, pfd[1]), "When sending message for writef\n");
			msg_destroy(msg, NULL, NULL);
			
			msg = msg_init();
			p = packet_appendf("woo", 4, "home/inia");
			assert(p);
			msg_make(msg, M_APPENDF, "home3", p);
			SYSCALL_EXIT(msg_send(msg, pfd[1]), "When sending message for appendf\n");
			msg_destroy(msg, NULL, NULL);

			msg = msg_init();
			p = packet_error(&error);
			assert(p);
			msg_make(msg, M_ERR, "home4", p);
			SYSCALL_EXIT(msg_send(msg, pfd[1]), "When sending message for error\n");
			msg_destroy(msg, NULL, NULL);

			msg = msg_init();
			p = packet_getf("abcdef", 7);
			assert(p);
			msg_make(msg, M_GETF, "home5", p);
			SYSCALL_EXIT(msg_send(msg, pfd[1]), "When sending message for getf\n");
			msg_destroy(msg, NULL, NULL);

			wait(&wstatus);
			close(pfd[1]);
			return 0;
		}
	}
	return 0;
}

#include <defines.h>
#include <tsqueue.h>
#include <fss.h>
#include <assert.h>

#define MAXCLIENTS 16

/* 135 bytes */
#define STRING "abcdefghijklmnopqrstuvwxyz1abcdefghijklmnopqrstuvwxyz2abcdefghijklmnopqrstuvwxyz3abcdefghijklmnopqrstuvwxyz4abcdefghijklmnopqrstuvwxyz5"
/* 82 bytes */
#define STR2 "nojwdneodnfjorenvojfenvjodb3'j233eok03dncoebdojen3dojcwnecpenckndpvcnj53bdihbedobd"
#define __DEBUG

fss_t fss;

/* This mutex is used by 'secondTest' function to print out fss_open and buf content in order */
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

/*
Test cases:
	1. A single thread tests: create - open failure - append - append - read - close,
		then writes on stdout: a) buffer, b) fss_dumpfile; then exits
	2. Two threads work on the SAME file as above as different clients and test:
		create failure - open (+ resize for both) - read - append / append - read,
		then write on stdout: buffer; threadMain writes on stdout: a) fss_dumpfile,
		then tests fss_clientCleanup and writes: fss_dumpall (test if correctly
		cleanups clients info).
	3. Four threads create 4 other files such that cache (writing) replacement
		takes up and close; threadMain then writes: fss_dumpall.
	4. Two threads create 10 other files such that cache (creating) replacement
		takes up and close; threadMain then writes: fss_dumpall.
	5. A single thread does a fss_readN of all files on the server and prints all
		their content on stdout.
	6. A single thread creates a file and exits. Then, two threads try to lock that file
		and sleep for 3 seconds before unlocking.
*/

/* Struct for passing arguments to threads */
struct arg_s {
	int who;
	char* pathname;
};


int whandler_stub(tsqueue_t* waitQueue){ return 0; }

int sbhandler_stub(void* content, size_t size, int cfd){ return 0; }

/** @brief First testcase thread function */
void* firstTest(struct arg_s* arg){
	char inbuf[] = "Servator1";
	char* buf;
	size_t size;
	assert(fss_create(&fss, arg->pathname, 16, arg->who, false, &whandler_stub) == 0);
	assert(fss_open(&fss, arg->pathname, arg->who, false) == -1);
	assert(fss_write(&fss, arg->pathname, inbuf, 9, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fss_write(&fss, arg->pathname, inbuf, 9, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fss_read(&fss, arg->pathname, &buf, &size, arg->who) == 0);
	assert(fss_close(&fss, arg->pathname, arg->who) == 0);
	if (write(1, buf, size) == -1) perror("write");
	free(buf);
	return NULL;
}


/** @brief Second testcase thread function */
void* secondTest(struct arg_s* arg){
	char inbuf[] = "Servator2";
	char* buf;
	size_t size;
	assert(fss_create(&fss, arg->pathname, 16, arg->who, false, &whandler_stub) == -1);
	
	pthread_mutex_lock(&mtx);
	if (fss_open(&fss, arg->pathname, arg->who, false) == -1) perror("fss_open"); /* One will success and the other will give the 'EBADF' error */
	pthread_mutex_unlock(&mtx);
	
	assert(fss_write(&fss, arg->pathname, inbuf, 9, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fss_read(&fss, arg->pathname, &buf, &size, arg->who) == 0);
	
	pthread_mutex_lock(&mtx);
	if (write(1, buf, size) == -1) perror("write");
	printf("\n");
	pthread_mutex_unlock(&mtx);
	
	fss_dumpfile(&fss, arg->pathname);
	free(buf);
	return NULL;
}


/** @brief Third testcase thread function */
void* thirdTest(struct arg_s* arg){
	char inbuf[] = STRING;
	char* buf;
	size_t size;
	assert(fss_create(&fss, arg->pathname, 16, arg->who, false, &whandler_stub) == 0);
	assert(fss_open(&fss, arg->pathname, arg->who, false) == -1);
	assert(fss_write(&fss, arg->pathname, inbuf, 135, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fss_read(&fss, arg->pathname, &buf, &size, arg->who) == 0);
	fss_close(&fss, arg->pathname, arg->who);
	free(buf);
	return NULL;
}


/** @brief Fourth testcase thread function */
void* fourthTest(struct arg_s* arg){
	assert(fss_create(&fss, arg->pathname, 20, arg->who, false, &whandler_stub) == 0);
	printf("fss_create successfully completed by arg->who == %d\n", arg->who);
	assert(fss_close(&fss, arg->pathname, arg->who) == 0);
	printf("arg->who == %d exiting...\n\n", arg->who);
	return NULL;
}


void* fifthTest(void* arg){
	llist_t* results;
	llistnode_t* node;
	fcontent_t* fc;
	assert((results = llist_init()));
	assert(fss_readN(&fss, 0, 0, results) == 0);
	printf("fifth_test: begin\n");
	llist_foreach(results, node){
		fc = (fcontent_t*)node->datum;
		printf("fifth_test: filename = %s\n", fc->filename);
		printf("fifth_test: size = %lu\n", fc->size);
		printf("fifth_test: content = \n");
		write(1, fc->content, fc->size);
		printf("\n");
	}
	printf("fifth_test: end\n");
	assert(llist_destroy(results, fcontent_destroy) == 0);
	return NULL;
}


void* sixthTest_createwrite(void* arg){
	char* filename = (char*)arg;
	llist_t* newowner;
	assert((newowner = llist_init()));
	assert(fss_create(&fss, filename, 20, 0, true, &whandler_stub) == 0); /* File is locked */
	assert(fss_write(&fss, filename, STR2, 82, 0, true, &whandler_stub, &sbhandler_stub) == 0); /* Write content */
	assert(fss_unlock(&fss, filename, 0, &newowner) == 0);
	printf("newowner_size = %d\n", newowner->size);
	if (newowner->size > 0) printf("newowner_clientId = %d\n", newowner->head->datum);
	assert(fss_close(&fss, filename, 0) == 0);
	assert(fss_clientCleanup(&fss, 0, &newowner) == 0);
	assert(llist_destroy(newowner, free) == 0);
	return NULL;
}


int main(void){

	pthread_t p[4];
	memset(p, 0, 4 * sizeof(pthread_t));

	struct arg_s args1_2;
	struct arg_s args3[4];
	struct arg_s args4[4];

	args1_2.who = 1; args1_2.pathname = "/home/servator/Scrivania/file1";

	for (int i = 0; i < 4; i++) args3[i].who = i;
	args3[0].pathname = "/home/servator/Scrivania/file2";
	args3[1].pathname = "/home/servator/Scrivania/file3";
	args3[2].pathname = "/home/servator/Scrivania/file4";	
	args3[3].pathname = "/home/servator/Scrivania/file5";
	
	for (int i = 0; i < 4; i++) args4[i].who = 10 + i;
	args4[0].pathname = "/home/servator/Scrivania/file6";
	args4[1].pathname = "/home/servator/Scrivania/file7";
	args4[2].pathname = "/home/servator/Scrivania/file8";
	args4[3].pathname = "/home/servator/Scrivania/file9";

	assert(fss_init(&fss, 4, 512, 6) == 0); /* 1/2 KB capacity and 6 maxFileNo */

	/* First test */
	pthread_create(&p[0], NULL, firstTest, &args1_2);
	pthread_join(p[0], NULL);
	printf("\nFIRST TEST RESULT:\n");
	fss_dumpAll(&fss);

	/* Second test */
	args1_2.who = 10; //FIXME Ora il resizing è "centralizzato" /* For testing correct resizing of fdata_t 'clients' field */
	pthread_create(&p[0], NULL, secondTest, &args1_2);
	pthread_create(&p[1], NULL, secondTest, &args1_2);
	pthread_join(p[0], NULL);
	pthread_join(p[1], NULL);
	printf("\nSECOND TEST RESULT:\n");
	int c[1];
	c[0] = 1;
	llist_t* l = llist_init();
	assert(l != NULL);
	fss_clientCleanup(&fss, 1, &l);
	printf("newowners_list size = %lu\n", l->size);
	llist_destroy(l, free);
	fss_dumpAll(&fss);

	/* Third test */
	for (int i = 0; i < 4; i++) pthread_create(&p[i], NULL, thirdTest, &args3[i]);
	for (int i = 0; i < 4; i++) pthread_join(p[i], NULL);	
	printf("\nTHIRD TEST RESULT:\n");
	fss_dumpAll(&fss);
	assert(fss.fmap->nentries == 3); /* The last file inserted should have expelled the first two */
	
	/* Fourth test */
	for (int i = 0; i < 4; i++) pthread_create(&p[i], NULL, fourthTest, &args4[i]);
	for (int i = 0; i < 4; i++) pthread_join(p[i], NULL);	
	printf("\nFOURTH TEST RESULT:\n");
	assert(fss.fmap->nentries == 6); /* File capacity should have been reached and one file rejected */
	fss_dumpAll(&fss);

	/* Fifth test */
	pthread_create(&p[0], NULL, fifthTest, NULL);
	pthread_join(p[0], NULL);
	printf("\nFIFTH TEST RESULT:\n");
	fss_dumpAll(&fss);

	/* Sixth test */
	pthread_create(&p[0], NULL, sixthTest_createwrite, "/home/servator/lorem_ipsum.txt");
	pthread_join(p[0], NULL);
	printf("\nSIXTH TEST RESULT:\n");
	fss_dumpAll(&fss);

	fss_destroy(&fss);
	return 0;
}

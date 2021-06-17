#include <defines.h>
#include <tsqueue.h>
#include <fs.h>
#include <assert.h>

#define MAXCLIENTS 16

/* 135 bytes */
#define STRING "abcdefghijklmnopqrstuvwxyz1abcdefghijklmnopqrstuvwxyz2abcdefghijklmnopqrstuvwxyz3abcdefghijklmnopqrstuvwxyz4abcdefghijklmnopqrstuvwxyz5"
/* 82 bytes */
#define STR2 "nojwdneodnfjorenvojfenvjodb3'j233eok03dncoebdojen3dojcwnecpenckndpvcnj53bdihbedobd"

#define LOREM_IPSUM "/home/servator/lorem_ipsum.txt"

#define __DEBUG


pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/*
Test cases:
	1. A single thread tests: create - open failure - append - append - read - close,
		then writes on stdout: a) buffer, b) fs_dumpfile; then exits
	2. Two threads work on the SAME file as above as different clients and test:
		create failure - open (+ resize for both) - read - append / append - read,
		then write on stdout: buffer; threadMain writes on stdout: a) fs_dumpfile,
		then tests fs_clientCleanup and writes: fs_dumpall (test if correctly
		cleanups clients info).
	3. Four threads create 4 other files such that cache (writing) replacement
		takes up and close; threadMain then writes: fs_dumpall.
	4. Two threads create 10 other files such that cache (creating) replacement
		takes up and close; threadMain then writes: fs_dumpall.
	5. A single thread does a fs_readN of all files on the server and prints all
		their content on stdout.
	6. A single thread creates and "uploads" a file with STR2 and exits.
		Then, three threads try to lock that file and write a string before unlocking.
*/


/* Struct for passing arguments to threads */
struct arg_s {
	int who;
	char* pathname;
	int* waitClients;
	FileStorage_t* fs;
};


int whandler_stub(tsqueue_t* waitQueue){ return 0; }

int sbhandler_stub(void* content, size_t size, int cfd){ return 0; }

/** @brief First testcase thread function */
void* firstTest(struct arg_s* arg){
	char inbuf[] = "Servator1";
	char* buf;
	size_t size;
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	assert(fs_create(fs, arg->pathname, arg->who, false, &whandler_stub) == 0);
	assert(fs_write(fs, arg->pathname, inbuf, 9, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fs_write(fs, arg->pathname, inbuf, 9, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fs_read(fs, arg->pathname, &buf, &size, arg->who) == 0);
	assert(fs_close(fs, arg->pathname, arg->who) == 0);
	if (write(1, buf, size) == -1) perror("write");
	free(buf);
	return NULL;
}


/** @brief Second testcase thread function */
void* secondTest(struct arg_s* arg){
	char inbuf[] = "Servator2";
	char* buf;
	size_t size;
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	assert(fs_create(fs, arg->pathname, arg->who, false, &whandler_stub) == -1);
	
	pthread_mutex_lock(&mtx);
	if (fs_open(fs, arg->pathname, arg->who, false) == -1) perror("fs_open"); /* Both will success */
	pthread_mutex_unlock(&mtx);
	
	assert(fs_write(fs, arg->pathname, inbuf, 9, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fs_read(fs, arg->pathname, &buf, &size, arg->who) == 0);

	pthread_mutex_lock(&mtx);
	if (write(1, buf, size) == -1) perror("write");
	printf("\n");
	pthread_mutex_unlock(&mtx);

	fs_dumpfile(fs, arg->pathname);
	free(buf);
	return NULL;
}


/** @brief Third testcase thread function */
void* thirdTest(struct arg_s* arg){
	char inbuf[] = STRING;
	char* buf;
	size_t size;
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	assert(fs_create(fs, arg->pathname, arg->who, false, &whandler_stub) == 0);
	assert(fs_write(fs, arg->pathname, inbuf, 135, arg->who, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fs_read(fs, arg->pathname, &buf, &size, arg->who) == 0);
	fs_close(fs, arg->pathname, arg->who);
	free(buf);
	return NULL;
}


/** @brief Fourth testcase thread function */
void* fourthTest(struct arg_s* arg){
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	assert(fs_create(fs, arg->pathname, arg->who, false, &whandler_stub) == 0);
	printf("fs_create successfully completed by arg->who == %d\n", arg->who);
	assert(fs_close(fs, arg->pathname, arg->who) == 0);
	printf("arg->who == %d exiting...\n\n", arg->who);
	return NULL;
}


void* fifthTest(struct arg_s* arg){
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	llist_t* results;
	llistnode_t* node;
	fcontent_t* fc;
	assert((results = llist_init()));
	assert(fs_readN(fs, 0, 0, &results) == 0);
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


void* sixthTest_createwrite(struct arg_s* arg){
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	char* filename = (char*)(arg->pathname);
	llist_t* newowner;
	assert((newowner = llist_init()));
	assert(fs_create(fs, filename, 0, true, &whandler_stub) == 0); /* File is locked */
	assert(fs_write(fs, filename, STR2, 82, 0, true, &whandler_stub, &sbhandler_stub) == 0); /* Write content */
	assert(fs_unlock(fs, filename, 0, &newowner) == 0);
	//assert(fs_remove(fs, filename, 0, &whandler_stub) == -1);
	printf("newowner_size = %d\n", newowner->size);
	if (newowner->size > 0) printf("newowner_clientId = %d\n", newowner->head->datum);
	assert(fs_close(fs, filename, 0) == 0);
	assert(fs_clientCleanup(fs, 0, &newowner) == 0);
	assert(llist_destroy(newowner, free) == 0);
	printf("sixthTest - exiting\n");
	return NULL;
}


void* sixthTest_locking(struct arg_s* arg){
	int id = ((struct arg_s*)arg)->who;
	char* filename = ((struct arg_s*)arg)->pathname;
	FileStorage_t* fs = ((struct arg_s*)arg)->fs;
	int* waitClients = ((struct arg_s*)arg)->waitClients;
	llist_t* newowner;
	assert((newowner = llist_init()));
	int ret;
	assert((ret = fs_open(fs, filename, id, true)) >= 0);

	pthread_mutex_lock(&mtx);
	if (ret == 1) waitClients[id]++; /* Client waiting */
	while (waitClients[id] > 0) { printf("%d waiting\n", id); pthread_cond_wait(&cond, &mtx); }
	if (ret == 1){
		ret = fs_open(fs, filename, id, true);
		if (ret < 0) perror("fs_open[locking]");
	}
	pthread_mutex_unlock(&mtx);

	assert(fs_write(fs, filename, "@A@", 3, id, false, &whandler_stub, &sbhandler_stub) == 0);
	assert(fs_close(fs, filename, id) == 0);
	llistnode_t* node;
	int* datum;

	assert(fs_clientCleanup(fs, id, &newowner) == 0);
	
	pthread_mutex_lock(&mtx);
	printf("newowners list size = %lu [client %d]\n", newowner->size, id);
	llist_foreach(newowner, node){
		datum = node->datum;
		printf("datum = %d\n", *datum);
		waitClients[*datum]--;
	}
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mtx);
	
	assert(llist_destroy(newowner, free) == 0);
	return NULL;
}


int main(void){

	pthread_t p[6];
	memset(p, 0, 6 * sizeof(pthread_t));

	int* waitClients = calloc(16, sizeof(int));

	struct arg_s args1_2;
	struct arg_s args3[4];
	struct arg_s args4[4];
	struct arg_s args5;
	struct arg_s args60;
	struct arg_s args6[3];

	args1_2.who = 1; args1_2.pathname = "/home/servator/Scrivania/file1";
	args5.who = 0;
	args60.who = 0;
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
	
	for (int i = 0; i < 3; i++){ args6[i].who = i+1; args6[i].pathname = LOREM_IPSUM; args6[i].waitClients = waitClients;}

	FileStorage_t* fs;
	
	assert((fs = fs_init(4, 512, 6, 24)) != NULL); /* 1/2 KB capacity, 6 maxFileNo and 25 possible clients (0-24) */

	args1_2.fs = fs;
	for (int i = 0; i < 3; i++){ args3[i].fs = fs; args4[i].fs = fs; args6[i].fs = fs; }
	args3[3].fs = fs; args4[3].fs = fs;
	
	args5.fs = fs;
	args60.pathname = LOREM_IPSUM;
	args60.fs = fs;
	
	/* First test */
	printf("\nFIRST TEST RESULT:\n");
	pthread_create(&p[0], NULL, firstTest, &args1_2);
	pthread_join(p[0], NULL);
	fs_dumpAll(fs);

	/* Second test */
	printf("\nSECOND TEST RESULT:\n");
	args1_2.who = 10; //FIXME Ora il resizing è "centralizzato" /* For testing correct resizing of fdata_t 'clients' field */
	pthread_create(&p[0], NULL, secondTest, &args1_2);
	pthread_create(&p[1], NULL, secondTest, &args1_2);
	pthread_join(p[0], NULL);
	pthread_join(p[1], NULL);
	llist_t* l = llist_init();
	assert(l != NULL);
	fs_clientCleanup(fs, 1, &l);
	printf("newowners_list size = %lu\n", l->size);
	llist_destroy(l, free);
	fs_dumpAll(fs);

	/* Third test */
	printf("\nTHIRD TEST RESULT:\n");
	for (int i = 0; i < 4; i++) pthread_create(&p[i], NULL, thirdTest, &args3[i]);
	for (int i = 0; i < 4; i++) pthread_join(p[i], NULL);	
	fs_dumpAll(fs);
	assert(fs->fmap->nentries == 3); /* The last file inserted should have expelled the first two */
	
	/* Fourth test */
	printf("\nFOURTH TEST RESULT:\n");
	for (int i = 0; i < 4; i++) pthread_create(&p[i], NULL, fourthTest, &args4[i]);
	for (int i = 0; i < 4; i++) pthread_join(p[i], NULL);	
	assert(fs->fmap->nentries == 6); /* File capacity should have been reached and one file rejected */
	fs_dumpAll(fs);

	/* Fifth test */
	printf("\nFIFTH TEST RESULT:\n");
	pthread_create(&p[0], NULL, fifthTest, &args5);
	pthread_join(p[0], NULL);
	fs_dumpAll(fs);

	/* Sixth test */
	printf("\nSIXTH TEST RESULT:\n");
	pthread_create(&p[0], NULL, sixthTest_createwrite, &args60);
	pthread_join(p[0], NULL);
	for (int i = 0; i < 3; i++) pthread_create(&p[i], NULL, sixthTest_locking, &args6[i]);
	for (int i = 0; i < 3; i++) pthread_join(p[i], NULL);
	fs_dumpAll(fs);

	fs_destroy(fs);
	
	free(waitClients);
	return 0;
}

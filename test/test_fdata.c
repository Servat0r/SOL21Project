#include <fdata.h>
#include <util.h>
#include <assert.h>

int main(int argc, char* argv[]){
	if (argc < 2) exit(1);
	void *buf1, *buf2, *buf3;
	size_t size;
	int fd = open(argv[1], O_RDONLY); /* Open disk file (and virtually connection) */
	FileData_t* fdata;
	fdata = fdata_create(128, fd, false);
	assert(fdata);
	buf1 = malloc(1024);
	memset(buf1, 0, 1024);
	ssize_t m = read(fd, buf1, 1024); /* Reads content from disk file */
	assert(m > 0);
	assert(fdata_open(fdata, fd, false) == -1); /* #Already open for fd! */
	assert(fdata_write(fdata, buf1, 1024, fd, false) == 0); /* Writes data to server file (newly created) */
	assert(fdata_read(fdata, &buf2, &size, fd, false) == 0); /* Reads data from server file (written before) */
	fdata_printout(fdata); /* fd open */
	assert(fdata_close(fdata, fd) == 0); /* Closes connection */
	assert(fdata_read(fdata, &buf3, &size, fd, false) == -1); /* #Already closed for fd! */
	assert(memcmp(buf1, buf2, 1024) == 0);
	int fd2 = open("/home/servator/Scrivania/try.txt", O_CREAT | O_RDWR);
	assert(fd2 >= 0);
	assert(fdata_open(fdata, fd2, false) == 0);
	buf3 = malloc(16);
	memset(buf3, 0, 16);
	strncpy(buf3, "ciao mondo try\n",16);
	assert(fdata_write(fdata, buf3, 16, fd2, false) == 0);
	free(buf3);
	fdata_printout(fdata);
	assert(fdata_read(fdata, &buf3, &size, fd2, false) == 0);			
	assert(write(fd2, buf3, 1040) > 0);
	fchmod(fd2, S_IRUSR | S_IWUSR);
	free(buf1);
	free(buf2);
	free(buf3);
	close(fd); /* Closes disk file */
	close(fd2);
	fdata_printout(fdata);
	fdata_destroy(fdata);
	return 0;
}

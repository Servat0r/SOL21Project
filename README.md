Operating Systems Project for the a.y. 2020/21.

Project description (Italian) can be found here:
http://didawiki.cli.di.unipi.it/lib/exe/fetch.php/informatica/sol/laboratorio21/progettosol-20_21.pdf

WARNING: L'ultimo commit è stato effettuato dopo la consegna del progetto;
l'ultimo commit prima della consegna del progetto è il #66.


`argparser.h` - Parser of command-line arguments.

`client.c` - Client program.

`client_server_API.h` - Given API for client communication with server.

`config.h` - Header file for server configuration and config file parsing.

`defines.h` - Common header files ( `stdio.h`, `stddef.h`, `stdlib.h`,`string.h`, `stdbool.h`, `errno.h`,`unistd.h`, `pthread.h`, `ctype.h`, `limits.h` ) and macros definitions. 

`dir_utils.h` - Utilities for loading and saving files in directories.

`fdata.h` - File data and metadata management system.

`fflags.h` - Public global flags for `fdata_t` objects.

`fs.h` - Filesystem implementation on top of `fdata.h` and `fflags.h`.

`icl_hash.h` - hash table implementation from Keith Seymour's proxy library code (full copyright notice in `icl_hash.c`)

`linkedlist.h` - (NOT concurrent) doubly linked list.

`parser.h` - Configuration settings parser for server.

`protocol.h` - Client-server request protocol.

`server_support.h` - Support data structures for server.

`server.c` - Server program.

`tsqueue.h` - Thread-safe FIFO queue with embedded iterator.

`util.h` - Miscellaneous utility functions and macros.

`config1.txt`, `config2.txt`, `config3.txt` - Configuration files for server (each one for the corresponding bash test).


#include <sys/time.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <assert.h>
#include <sstream>
#include <errno.h>
#include "ubench/fs/fs.h"
#include "ubench/time.h"

static int
usage() 
{
	return -1;
}


static int 
__ubench_fs_seqwrite(const char* root, int numops, int warmup_ops, size_t size)
{
	MEASURE_TIME_PREAMBLE
	int                    ret = 0;
	unsigned long long     runtime;
	hrtime_t               runtime_cycles = 0;
	unsigned long long     sync_runtime;
	hrtime_t               sync_runtime_cycles;
	int                    fd;
	void*                  buf = new char[size];
	unsigned long		totalsize=0, onegig = 1024*1024*1024;
	unsigned long		exp_nr_writes = 0, nr_writes = 0;

	std::stringstream  ss;
        ss << std::string(root);
        ss << "/test.dat";

	/* file creation */
	fd = fs_open2(ss.str().c_str(), O_CREAT|O_TRUNC|O_RDWR, S_IRWXU);
        assert(fd>0);
	/* populate the file with one gig of data */
	while(totalsize < onegig)
	{	
        	ret = fs_write(fd, buf, size);
		assert(ret == size);
		totalsize += size;
	}
	fs_close(fd);
	fs_fsync(fd);
	fs_sync();
	system("echo 3 >> /proc/sys/vm/drop_caches");
	exp_nr_writes = totalsize/size;
	printf("file creation %s is done\n", ss.str().c_str());

	MEASURE_TIME_START

	for (int i=0; i<numops; i++) {
		totalsize = 0;
		fd = fs_open(ss.str().c_str(), O_RDWR);
		assert(fd>0);
    	MEASURE_CYCLES_START
		while(totalsize < onegig)
		{	
        		ret = fs_write(fd, buf, size);
			assert(ret == size);
			totalsize += size;
		}
    	MEASURE_CYCLES_STOP
		ADD_MEASURE_TIME_DIFF_CYCLES(runtime_cycles)
		fs_fsync(fd);
		fs_sync();
		fs_close(fd);
		printf("write %d done\n", i);
	}

	MEASURE_TIME_STOP

    MEASURE_TIME_DIFF_USEC(runtime)
    //MEASURE_TIME_DIFF_CYCLES(runtime_cycles)
	
	std::cout << "SEQWRITE\n" << measure_time_summary(numops*exp_nr_writes, runtime, runtime_cycles) << std::endl;
	return ret;
}


int
ubench_fs_seqwrite(int argc, char* argv[])
{
	extern int  optind;
	extern int  opterr;
	char        ch;
	int         numops = 0;
	char*       objtype;
	const char* root_path = NULL;
	size_t      size = 0;
	int         warmup_ops = 0;
	
	opterr=0;
	optind=0;
	while ((ch = getopt(argc, argv, "p:n:s:w:"))!=-1) {
		switch (ch) {
			case 'p': // root path
				root_path = optarg;
				break;
			case 'n':
				numops = atoi(optarg);
				break;
			case 'w':
				warmup_ops = atoi(optarg);
				break;
			case 's':
				size = atoi(optarg);
				break;
			case '?':
				usage();
			default:
				break;
		}
	}

	if (!root_path) {
		return -1;
	}

	return __ubench_fs_seqwrite(root_path, numops, warmup_ops, size);
}

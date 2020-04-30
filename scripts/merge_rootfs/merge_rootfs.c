#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>
#include<unistd.h>
#include <sys/time.h>
#include <unistd.h>

#define IMAGE_SIZE	0x400000
#define SHELL "/bin/sh"

#define __u32			unsigned int
#define ___swab32(x) \
        ((__u32)( \
                (((__u32)(x) & (__u32)0x000000ffUL) << 24) | \
                (((__u32)(x) & (__u32)0x0000ff00UL) <<  8) | \
                (((__u32)(x) & (__u32)0x00ff0000UL) >>  8) | \
                (((__u32)(x) & (__u32)0xff000000UL) >> 24) ))



int runShell (const char *command)
{
        int status;
        pid_t pid;

        if ((pid = vfork()) == 0)
        {
                execl (SHELL, SHELL, "-c", command, NULL);
                _exit (1);
        }

        else if (pid < 0)
        {
                status = -1;
        }

        else
        {
                status = 0;
                if (waitpid (pid, &status, 0) != pid)
                {
                        status = -1;
                }
        }
        return status;
}


int main(int argc,char *argv[])
{
	FILE *fp,*tfp; 	
	
	unsigned int offset;
	unsigned int buffer;
	unsigned int rootfs_size, size=0;
	FILE *fd;
	struct stat st;
	int i = 0;
	char* string  = (char*)malloc(1024);

	fp=fopen(argv[1],"r");	
	tfp=fopen(argv[3],"a+");
	
	stat(argv[2],&st);
	rootfs_size = st.st_size;

	offset=IMAGE_SIZE - 0x7fc0;
	size = 0x400000 - 0x8000;
	size += rootfs_size;
	printf("rootfs_size = 0x%x\n",rootfs_size);
	printf("size = 0x%x\n",size);
	while(i < offset)
	{
		buffer = 0;
		fread(&buffer,sizeof(int),1,fp);
		if(i == 0xC)
		{
			buffer= ___swab32(size);
		}
		fwrite(&buffer,1,sizeof(int),tfp );
		i+=4;
	}
	fclose(fp);
	fclose(tfp);
	sprintf(string,"cat %s >> %s\n",argv[2],argv[3]);
	runShell(string);

}

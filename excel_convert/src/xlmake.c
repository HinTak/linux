/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 *
 *Author : Harinath A
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#define head_1 " "
//#define head_2 "\n\nThe result of the measurements\n\n\t\t\t\t/This Board\n\tCPU/DDR/bus_clock(MHz)\tCPU/DDR/bus_clock(MHz)\tDataType\t"
#define head_2 "\nResult\t\t\t\tEchoP\t\t\tFoxP\t\t\tGolfP\t\t\tHawkP\t\t\tHawkM\t\t\tHawkP-AV\t\t\n"
#define XLS_NAME "config_output"
#define CONFIG_FNAME "Kernel_Config_EXCEL.csv"
void header_xls()
{
	FILE *fwr;
        fwr = fopen(XLS_NAME,"w");
        if(fwr == NULL)
                return;
        fwrite(head_1,strlen(head_1),1,fwr);
	fclose(fwr);
	//system("echo `date` >>"XLS_NAME);
	fwr = fopen(XLS_NAME,"a+");
        if(fwr == NULL)
                return;
        fwrite(head_2,strlen(head_2),1,fwr);
        fclose(fwr);
}

struct _tags{
	char *search;
	char *component_1;
	char *component_2;
	char *data_type;
};

int write_(char *name,struct _tags *tagss,char *fpread)
{
	FILE *fwr,*fre;
	int _var = 0;
	char *data = NULL,*tmp,*prev;
	fwr = fopen(XLS_NAME,"a+");
	if(fwr == NULL)
		return -1;
	fre = fopen(fpread,"r");
	if(fre == NULL)
	{
		fclose(fwr);
		printf("error :1\n");
		return -1;
	}
	fseek(fre,0,2);
	_var =  ftell(fre);
	fseek(fre,0,0);
	data = (char *) calloc(sizeof(char),_var+1);
	if(data == NULL)
	{
		fclose(fwr);	
		fclose(fre);		
		return -1;	
	}
	fread(data,_var,sizeof(char),fre);
	fclose(fre);
	tmp = data;
	if(tmp = strstr(data,tagss[0].search))
		for(_var = 0;tagss[_var].search;_var++)
		{
			fwrite(name,strlen(name),1,fwr);//Name
			fwrite("\t",1,1,fwr);
			fwrite(tagss[_var].component_1,strlen(tagss[_var].component_1),sizeof(char),fwr);
			fwrite("\t",1,1,fwr);
			fwrite(tagss[_var].component_2,strlen(tagss[_var].component_2),sizeof(char),fwr);
			fwrite("\t",1,1,fwr);
			fwrite(tagss[_var].data_type,strlen(tagss[_var].data_type),sizeof(char),fwr);
			fwrite("\t",1,1,fwr);
			if(tmp && (tmp=strstr(tmp,tagss[_var].search)))
			{
				char buffr[1024];
				int index = 0;
				tmp = tmp+strlen(tagss[_var].search);
				while(tmp && *tmp )
				{
					if(*tmp >= '0' && *tmp <= '9')
						break;
					if(*tmp == 'i' && !strncmp(tmp,"inf",3))
						break;
					tmp++;
				}
				while(tmp && *tmp && *tmp != ' ' )
				{		
					if(*tmp == '\n')
						break;

					buffr[index] = *tmp;
					tmp++;
					index++;
				}
				prev = tmp;
				buffr[index] = '\0';
				fwrite(buffr,strlen(buffr),sizeof(char),fwr);
			}
			else
			{
				tmp = prev;
				fwrite("------",6,1,fwr);
			}
			fwrite("\n",1,1,fwr);
		}
	free(data);
	fclose(fwr);	
}

void write_to(char *filename)
{
	FILE *fre,*fwr;
        char vl;
        fwr = fopen(XLS_NAME,"a+");
        if(fwr == NULL)
	{
                return ;
	}
        fre = fopen(filename,"r");
        if(fre == NULL)
        {
                fclose(fwr);
                printf("error :2\n");
                return ;
        }
        while(fread(&vl,1,1,fre)>0)
        {
                fwrite(&vl,1,1,fwr);
        }
        fclose(fwr);
        fclose(fre);

}

void config()
{
	write_to(CONFIG_FNAME);
}
int main()
{
	char dump_buff[8096];
	header_xls();
	config();
	txtToXMLExcel(XLS_NAME);
	return 0;
}

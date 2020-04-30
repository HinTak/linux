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
#define XML_HEADER "<?xml version=\"1.0\"?>\n"
#define WORKBOOK_TAG1 "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\"\n\
 xmlns:o=\"urn:schemas-microsoft-com:office:office\"\n\
 xmlns:x=\"urn:schemas-microsoft-com:office:excel\"\n\
 xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\"\n\
 xmlns:html=\"http://www.w3.org/TR/REC-html40\">\n"

#define STYLES_START "<Styles>\n"
#define STYLES_END "</Styles>\n"

#define STYLE_1 "<Style ss:ID=\"s1\">\n\
<Alignment ss:Vertical=\"Center\"/>\n\
<Borders>\n\
<Border ss:Position=\"Bottom\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Left\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Right\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Top\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
</Borders>\n\
<Font x:Family=\"Swiss\" ss:Color=\"#FFFFFF\" ss:Bold=\"1\"/>\n\
<Interior ss:Color=\"#FFFFFF\" ss:Pattern=\"Solid\"/>\n\
</Style>\n"
#define STYLE_2 "<Style ss:ID=\"s2\">\n\
	<Alignment ss:Vertical=\"Center\"/>\n\
<Borders>\n\
<Border ss:Position=\"Bottom\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Left\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Right\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Top\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
</Borders>\n\
<Font x:Family=\"Swiss\" ss:Color=\"#FFFFFF\"  ss:Bold=\"1\"/>\n\
<Interior ss:Color=\"#FFFFFF\" ss:Pattern=\"Solid\"/>\n\
</Style>\n"
#define STYLE_3 "<Style ss:ID=\"s3\">\n\
	<Alignment ss:Vertical=\"Center\"/>\n\
<Borders>\n\
<Border ss:Position=\"Bottom\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Left\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Right\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
<Border ss:Position=\"Top\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\"/>\n\
</Borders>\n\
<Font x:Family=\"Swiss\" ss:Bold=\"0\"/>\n\
<Interior ss:Color=\"#FFFEEE\" ss:Pattern=\"Solid\"/>\n\
</Style>\n"

#define WORKBOOK_TAG2 "\n</Workbook>\n"
#define WORK_SHEET_E1 "<Worksheet ss:Name=\"Measurements\">\n"
#define WORK_SHEET_X1 "\n</Worksheet>\n"

#define COLUMNS "<Column ss:Width=\"150\"/>\n\
<Column ss:Width=\"80\"/>\n\
<Column ss:Width=\"80\"/>\n\
<Column ss:Width=\"80\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>\n\
<Column ss:Width=\"230\"/>"

#if 0
#define Table_TAG1 "<ss:Table>"
#define Table_TAG2 "\n</ss:Table>\n"
#define ROW_E1 "<ss:Row ss:Height=\"15\">"
#define ROW_X1 "\n</ss:Row>\n"
#define CELL_TAGE1 "<ss:Cell "
#define CELL_TAGE2 "\n</ss:Cell>\n"
#define DATA_E1 "<ss:Data ss:Type=\"String\">"
#define DATA_X1 "</ss:Data>"
#else
#define Table_TAG1 "<Table>"
#define Table_TAG2 "\n</Table>\n"
#define ROW_E1 "<Row ss:Height=\"15\">"
#define ROW_X1 "\n</Row>\n"
#define CELL_TAGE1 "<Cell "
#define CELL_TAGE2 "\n</Cell>\n"
#define DATA_E1 "<Data ss:Type=\"String\">"
#define DATA_E2 "<Data ss:Type=\"Number\">"
#define DATA_X1 "</Data>"
#define CELL_STYLE " ss:StyleID="
#define STYLE_S1 "\"s1\""
#define STYLE_S2 "\"s2\""
#define STYLE_S3 "\"s3\""

#endif


#define SEARCH_TAGS1 "\t,"
#define SEARCH_TAGS2 "\n"
#define XL_ERROR -1

#define XLS ".xls"
#define MERGE_DOWN "ss:MergeDown="
#define INDEX_1 " ss:Index=\"2\""

#define FOUND_TAG1 "CPU"
#define FOUND_TAG2 "KERNEL"
#define FOUND_TAG3 "Memory"
#define FOUND_TAG4 "Filesystem"
#define FOUND_TAG5 "Network"

int txtToXMLExcel(char *fileName)
{
	int ROW_OPENED = 0,CELL_OPENED = 0;
	FILE *openFile,*writeFile;
	char *nData = NULL;
	int size = 0;
	int tag_1=0,tag_2=0,tag_3=0,tag_4=0,tag_5=0;
	char wFileName[1024];
	if((openFile = fopen(fileName,"r")) == NULL)
	{
		printf("Error to open %s\n",fileName);
		return XL_ERROR;
	}
	fseek(openFile,0,2);
	size = ftell(openFile);
	fseek(openFile,0,0);
	if((nData = (char *) calloc(size,sizeof(char)+1)) == NULL)
	{
		printf("Error to alloc \n");
		return XL_ERROR;
	}
	fread(nData,size,sizeof(char),openFile);
	fclose(openFile);
	strcpy(wFileName,fileName);
	strcat(wFileName,XLS);
	if((writeFile = fopen(wFileName,"w")) == NULL)
	{
		printf("Error to open %s\n",wFileName);
		return XL_ERROR;
	}
	fwrite(XML_HEADER,strlen(XML_HEADER),sizeof(char),writeFile);
	fwrite(WORKBOOK_TAG1,strlen(WORKBOOK_TAG1),sizeof(char),writeFile);
	fwrite(STYLES_START,strlen(STYLES_START),sizeof(char),writeFile);
	fwrite(STYLE_1,strlen(STYLE_1),sizeof(char),writeFile);
	fwrite(STYLE_2,strlen(STYLE_2),sizeof(char),writeFile);
	fwrite(STYLE_3,strlen(STYLE_3),sizeof(char),writeFile);
	fwrite(STYLES_END,strlen(STYLES_END),sizeof(char),writeFile);
	fwrite(WORK_SHEET_E1,strlen(WORK_SHEET_E1),sizeof(char),writeFile);
	fwrite(Table_TAG1,strlen(Table_TAG1),sizeof(char),writeFile);
	fwrite(COLUMNS,strlen(COLUMNS),sizeof(char),writeFile);
	fwrite(ROW_E1,strlen(ROW_E1),sizeof(char),writeFile);
	fwrite(CELL_TAGE1,strlen(CELL_TAGE1),sizeof(char),writeFile);
	fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
	fwrite(STYLE_S3,strlen(STYLE_S1),sizeof(char),writeFile);
	fwrite(">",1,1,writeFile);
	fwrite(DATA_E1,strlen(DATA_E1),sizeof(char),writeFile);
	ROW_OPENED = 1;
	CELL_OPENED = 1;
	for(size = 0; nData[size] ;size++)
	{
		if(nData[size] && nData[size] == SEARCH_TAGS1[0] || nData[size] == SEARCH_TAGS1[1])
		{
			int i = 1,is_number = 0;
			while(nData[size+i] != SEARCH_TAGS1[0] && nData[size+i] != SEARCH_TAGS1[1] && nData[size+i] != SEARCH_TAGS2[0])
			{
				if(nData[size+i] >= '0' && nData[size+i] <= '9' || nData[size+i] == '.' || nData[size+i] == ' ' )
				{
					is_number = 1;
				}
				else
				{
					is_number = 0;
					break;
				}
				i++;
			}
			i = 1;
			if(is_number)
			{
				while(nData[size+i] != SEARCH_TAGS1[0] && nData[size+i] != SEARCH_TAGS1[1] && nData[size+i] != SEARCH_TAGS2[0])
				{
					if(nData[size+i] == ' ')
					{
						int y = i;
						while(nData[size+y])
						{
							nData[size+y] = nData[size+y+1];	
							y++;
						}
						i--;
					}
					i++;
				}
			}
			fwrite(DATA_X1,strlen(DATA_X1),sizeof(char),writeFile);
			fwrite(CELL_TAGE2,strlen(CELL_TAGE2),sizeof(char),writeFile);
			fwrite(CELL_TAGE1,strlen(CELL_TAGE1),sizeof(char),writeFile);
			fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
			fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
			fwrite(">",1,1,writeFile);
			if(is_number)
				fwrite(DATA_E2,strlen(DATA_E2),sizeof(char),writeFile);
			else
				fwrite(DATA_E1,strlen(DATA_E1),sizeof(char),writeFile);
			continue;
		}
		if(nData[size] == SEARCH_TAGS2[0] )
		{
			fwrite(DATA_X1,strlen(DATA_X1),sizeof(char),writeFile);
			fwrite(CELL_TAGE2,strlen(CELL_TAGE2),sizeof(char),writeFile);
			fwrite(ROW_X1,strlen(ROW_X1),sizeof(char),writeFile);
			fwrite(ROW_E1,strlen(ROW_E1),sizeof(char),writeFile);
			fwrite(CELL_TAGE1,strlen(CELL_TAGE1),sizeof(char),writeFile);
			if(strncmp(nData+size+1,FOUND_TAG1,strlen(FOUND_TAG1)) == 0)
			{
				if(tag_1){
					while(nData[size] != '\t' && nData[size] != ',')
					{
						size++;
					}
					fwrite(INDEX_1,strlen(INDEX_1),sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
				}
				else
				{
					fwrite(MERGE_DOWN"\"34\"",strlen(MERGE_DOWN)+4,sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S1,strlen(STYLE_S1),sizeof(char),writeFile);
				}
				tag_1 = 1;
			}
			else if(strncmp(nData+size+1,FOUND_TAG2,strlen(FOUND_TAG2)) == 0)
			{
				if(tag_2){
					while(nData[size] != '\t' && nData[size] != ',')
					{
						size++;
					}
					fwrite(INDEX_1,strlen(INDEX_1),sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
				}
				else
				{
					fwrite(MERGE_DOWN"\"16\"",strlen(MERGE_DOWN)+4,sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S2,strlen(STYLE_S2),sizeof(char),writeFile);
				}
				tag_2 = 1;
			}
			else if(strncmp(nData+size+1,FOUND_TAG3,strlen(FOUND_TAG3)) == 0)
			{
				if(tag_3){
					while(nData[size] != '\t' && nData[size] != ',')
					{
						size++;
					}
					fwrite(INDEX_1,strlen(INDEX_1),sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
				}
				else
				{
					fwrite(MERGE_DOWN"\"43\"",strlen(MERGE_DOWN)+4,sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S1,strlen(STYLE_S1),sizeof(char),writeFile);
				}
				tag_3 = 1;
			}
			else if(strncmp(nData+size+1,FOUND_TAG4,strlen(FOUND_TAG4)) == 0)
			{
				if(tag_4){
					while(nData[size] != '\t' && nData[size] != ',')
					{
						size++;
					}
					fwrite(INDEX_1,strlen(INDEX_1),sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
				}
				else
				{
					fwrite(MERGE_DOWN"\"3\"",strlen(MERGE_DOWN)+3,sizeof(char),writeFile);
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S2,strlen(STYLE_S2),sizeof(char),writeFile);
				}
				tag_4 = 1;
			}
			else
				if(strncmp(nData+size+1,FOUND_TAG5,strlen(FOUND_TAG5)) == 0)
				{
					if(tag_5){
						while(nData[size] != '\t' && nData[size] != ',')
						{
							size++;
						}
						fwrite(INDEX_1,strlen(INDEX_1),sizeof(char),writeFile);
						fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
						fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
					}
					else
					{
						fwrite(MERGE_DOWN"\"3\"",strlen(MERGE_DOWN)+3,sizeof(char),writeFile);
						fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
						fwrite(STYLE_S1,strlen(STYLE_S1),sizeof(char),writeFile);
					}
					tag_5 = 1;
				}
				else
				{
					fwrite(CELL_STYLE,strlen(CELL_STYLE),sizeof(char),writeFile);
					fwrite(STYLE_S3,strlen(STYLE_S3),sizeof(char),writeFile);
				}
			fwrite(">",1,1,writeFile);
			fwrite(DATA_E1,strlen(DATA_E1),sizeof(char),writeFile);
			continue;
		}
		fwrite(&nData[size],1,1,writeFile);
	}
	fwrite(DATA_X1,strlen(DATA_X1),sizeof(char),writeFile);
	fwrite(CELL_TAGE2,strlen(CELL_TAGE2),sizeof(char),writeFile);
	fwrite(ROW_X1,strlen(ROW_X1),sizeof(char),writeFile);
	fwrite(Table_TAG2,strlen(Table_TAG2),sizeof(char),writeFile);
	fwrite(WORK_SHEET_X1,strlen(WORK_SHEET_X1),sizeof(char),writeFile);
	fwrite(WORKBOOK_TAG2,strlen(WORKBOOK_TAG2),sizeof(char),writeFile);
	free(nData);
	return 0;
}
#if MAIN
int main(int argc,char **argv)
{
	if(argc == 2)
	{
		txtToXMLExcel(argv[1]);
	}
	else
	{
		printf("exe <filename>");
	}
}
#endif

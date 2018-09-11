#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define STARTSIZE 1024
#define SECTORSIZE 512
#define NAMESIZE 11
#define LASTCLUSTER 0xFEF

unsigned char fatTable[SECTORSIZE];
char dirOutput[100];
int dirLength;
int totalFile;

//This function converts the number part of
//the output file name into a string and store 
//it in the input buffer
void nameToChar(int input, char* buffer){
	int counter=1;
	int totalD=0;

	do{
		counter*=10;
		totalD+=1;
	}while(input/counter!=0);

	counter/=10;

	int a=0;
	do{
		buffer[a++]=(char)(input/counter+48);
		input%=counter;
		counter/=10;
	}while(counter!=0);

	buffer[totalD]='\0';
}

//This function parse the directory entry, placing the name in the input buffer
//sotre the file size in the input fileSize address, and then return a bool value
//indicating if the input entry is a file or not
bool getName(char* buffer, unsigned int * fileSize, unsigned char* toParse){
	int counter=0;
	int actualCopy=-1;
	bool isDirectory=true;

	//below is an implementation just to assemble the correct pieces
	//of a file or directory name
	for(int a=0;a<NAMESIZE;a++){
		buffer[a]='\0';
	}

	while(counter<NAMESIZE-3){
		buffer[counter]=toParse[counter];

		if(counter==0&&((unsigned int)toParse[counter]&0xff)==0xE5)
			buffer[counter]='_';
		
		if((char)toParse[counter]!=' '){
			actualCopy=counter;
		}

		counter++;
	}

	int tempMark=actualCopy+2;

	while(counter<NAMESIZE){
		if((char)toParse[counter]!=' '){
			isDirectory=false;
		}

		if(!isDirectory){
			buffer[tempMark++]=(int)toParse[counter];
		}

		counter++;
	}

	if(isDirectory)
		buffer[actualCopy+1]='\0';
	else{
		buffer[actualCopy+1]='.';

		if(tempMark!=NAMESIZE-1)
			buffer[tempMark]='\0';
	}


	//start parsing file size
	(*fileSize)=0;

	counter=0;

	while(counter<4){
		unsigned int i=(unsigned int)toParse[28+counter]&0xff;
		i=i<<(counter*8);
		(*fileSize)=(*fileSize)|i;
		counter++;
	}

	return isDirectory;
}

//This function gets the start cluster from the given entry
int getIDdir(unsigned char* toParse){
	unsigned int clusterID=0;

	clusterID=clusterID|toParse[26];
	clusterID=clusterID|(((unsigned int)toParse[27]&0xff)<<8);

	return clusterID;
}

//This function finds the the next cluster from the fat table
//given the current cluster number
int findNextClusterFat(unsigned int inputID){
	int idDiv=inputID/2;
	int idMod=inputID%2;

	unsigned int nextID=0;
		
	if(idMod){
		nextID=nextID|((fatTable[idDiv*3+idMod]>>4)&0xf);
		nextID=nextID|(((unsigned int)fatTable[idDiv*3+idMod+1]<<4)&0xfff);
		nextID=nextID&0xfff;
	}
	else{
		nextID=nextID|fatTable[idDiv*3];
		nextID=nextID|(((unsigned int)fatTable[idDiv*3+1]<<8)&0xfff);
		nextID=nextID&0xfff;
	}
	
	if(nextID==0xfff||nextID<0x002||nextID>0xfef){
		return -1;
	}

	return nextID;	
}

//This function revcover any files, either deleted or not deleted
//The input information is based on previous functions
void recoverNor(char* nameBuffer, unsigned int size, unsigned char* toParse,unsigned char* bigImage,bool isDeleted){

	if(size>=0){

		//Setup the output file name with targetd output directory
		int i=0;
		int k=0;
		char tempBuffer[20]={'f','i','l','e','\0'};
		char tempNum[10]={'\0'};
		char tempExt[10]={'\0'};
		nameToChar(totalFile,tempNum);
		strcat(tempBuffer,tempNum);
		bool startCopy=false;

		totalFile++;
		dirOutput[dirLength]='/';
		dirOutput[dirLength+1]='\0';
		strcat(dirOutput,tempBuffer);

		//add extension to the new file name
		while(nameBuffer[i]!='\0'){

			if(nameBuffer[i]=='.')startCopy=true;

			if(startCopy)
				tempExt[k++]=nameBuffer[i];
			i++;
		}

		tempExt[k]='\0';

		strcat(dirOutput,tempExt);

		//Start recovering file, basically going cluster after cluster
		//until it is a FFF (for existing file) or a non zero number (for deleted file)
		FILE *outFile=fopen(dirOutput,"wb");
		unsigned int nextID=getIDdir(toParse);

		do{

			if(size>SECTORSIZE){
				fwrite(bigImage+(31+nextID)*SECTORSIZE,SECTORSIZE,1,outFile);
				size-=SECTORSIZE;
			}else{
				fwrite(bigImage+(31+nextID)*SECTORSIZE,size,1,outFile);
				size=0;
			}

			if(!isDeleted){
				nextID=findNextClusterFat(nextID);
				if(nextID==-1)
					break;
			}
			else{
				nextID+=1;

				if(findNextClusterFat(nextID)!=0)
					break;
			}

		}while((nextID+31)<=(unsigned int)LASTCLUSTER&&size>0);

		//close the file
		fclose(outFile);
	
	}
}

//This function goes through the whole image recursively
//it will keep probing and recovering files along the way
void goThroughDir(char* buffer, char* dirPath, unsigned int* fileSize,
				  unsigned char* bigImage,int* curSize, unsigned char* toParse){

	int a=0;
	int k=0;
	while((int)(toParse[a]&0xff)!=0&&(a<(SECTORSIZE))){

		bool isDirectory=getName(buffer,fileSize,toParse+a);

		//if this is a directory, parse the name of the directory
		//record it for the current recursive thread and keep probing
		if(isDirectory){

			//record the orginal directory name (by recording array position)
			int tempRecord=(*curSize);

			dirPath[(*curSize)++]='/';

			int i=0;
			while(buffer[i]!='\0'&&(i<NAMESIZE-3)){
				dirPath[(*curSize)++]=buffer[i];
				i++;
			}

			//relloc every time to make the buffer sufficient
			dirPath=realloc(dirPath,(*curSize)+NAMESIZE-3);

			int nextSector=getIDdir(toParse+a)+31;

			//Call the function recursively and keep probing
			//Skip the first two entries since they are useless
			//information in this case
			goThroughDir(buffer,dirPath,fileSize,bigImage,
					curSize,bigImage+SECTORSIZE*nextSector+32*2);

			int x=tempRecord;
			while(x<(*curSize)){
				dirPath[x++]='\0';
			}

			//if the function returns
			//restore the directory name
			(*curSize)=tempRecord;
		}else{

			//if this is a file, prepare the output string
			dirPath[*curSize]='/';
			dirPath[(*curSize)+1]='\0';
			
			char status[10];
			memcpy(status,"NORMAL",7);
			
			//recover the files into targeted directory
			if(buffer[0]=='_'){
				memcpy(status,"DELETED",8);
				recoverNor(buffer,*fileSize,toParse+a,bigImage,true);
			}
			else{
				recoverNor(buffer, *fileSize,toParse+a,bigImage,false);
			}
			
			//output information to the terminal
			fprintf(stdout,"FILE\t%s\t%s%s\t%d\n",status,dirPath,buffer,*fileSize);
			k=0;
			
			//restore buffer
			while(k<NAMESIZE){
				buffer[k++]='\0';
			}
			
			(*fileSize)=0;

		}

		//Each directory entry is 32 bytes, so every time
		//move to the next entry
		a+=32;
	}
}



int main(int argc, char*argv[]){

	//Parsing the initial disk image
	unsigned char* diskImage=malloc(STARTSIZE);
	unsigned char* rootDir=malloc(SECTORSIZE);
	char* dirPath=malloc(NAMESIZE);
	char* fileName=malloc(NAMESIZE+1);
	totalFile=0;

	dirLength=0;
	while(argv[2][dirLength]!='\0'){
		dirOutput[dirLength]=argv[2][dirLength];
		dirLength++;
	}
	
	printf("Pass\n");

	FILE *fp=fopen(argv[1],"rb");

	unsigned char temp=fgetc(fp);
	int sizeCount=0;
	int limit=STARTSIZE;
	
	//Store the directory name, fatTable, into the global arrays
	while(1){
		diskImage[sizeCount]=temp;
		sizeCount++;
		
		if(feof(fp))break;

		if(sizeCount==limit){
			limit*=2;
			diskImage=realloc(diskImage,limit);
		}
		temp=fgetc(fp);
	}

	//Store the root directory into a single array
	int a=0;
	while(a<SECTORSIZE){
		fatTable[a]=diskImage[SECTORSIZE+a];
		rootDir[a]=diskImage[SECTORSIZE*19+a];		
		a++;
	}

	unsigned int tempSize=0;
	int curSize=0;
	int k=0;
	while(k<curSize){
		dirPath[k++]='\0';
	}

	//Start parsing from the root Directory
	int tempTest=0;
	goThroughDir(fileName,dirPath,&tempSize,diskImage,&tempTest,rootDir);

	//close the file
	fclose(fp);
}

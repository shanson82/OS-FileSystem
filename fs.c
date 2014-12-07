#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>

// structure to store Master Boot Record information
typedef struct __attribute__ ((__packed__)) {
	uint16_t sector_size; // bytes ( >= 64 bytes)
	uint16_t cluster_size; // number of sectors (at least 1 sector/cluster) 
	uint16_t disk_size; // size of disk in clusters
	uint16_t fat_start;
	uint16_t fat_length; // number of clusters
	uint16_t data_start; 
	uint16_t data_length; // clusters
	char* disk_name;
} mbr_t;

typedef struct __attribute__ ((__packed__)) {
	uint8_t entry_type;
	uint16_t creation_date;
	uint16_t creation_time;
	uint8_t name_len;
	char name[16];
	uint32_t size;
} entry_t;

typedef struct __attribute__ ((__packed__)) {
	uint8_t type;
	uint8_t reserved;
	uint16_t start;
} entry_ptr_t;

// global variables
mbr_t *MBR_memory; 
uint16_t *FAT_memory;
uint8_t *DATA_memory; // try with 1-d array 

// format the date for creation_date and creation_time fields of entry_t struct
uint32_t date_format() {
	time_t t = time(NULL);
	struct tm *tptr = localtime(&t);
	uint32_t time_stamp;
	time_stamp = ((tptr->tm_year-80)<<25) + ((tptr->tm_mon+1)<<21) + ((tptr->tm_mday)<<16) + (tptr->tm_hour<<11) + (tptr->tm_min<<5) + ((tptr->tm_sec)%60)/2;
	return time_stamp;
}



// format the file system:
// determine FAT area length and Data area length
// write the Master Boot Record to file, initialize the FAT area, and create the root dir
void format(uint16_t sector_size, uint16_t cluster_size, uint16_t disk_size) {
	mbr_t *MBR = (mbr_t *)malloc(sizeof(mbr_t));
	MBR->sector_size = sector_size;
	MBR->cluster_size = cluster_size;
	MBR->disk_size = disk_size;
	MBR->fat_start = 1;
	MBR->disk_name = "A\0";

	// determine the size (in clusters) of the FAT and Data areas
	int i;
	int cluster_size_bytes = sector_size * cluster_size; // number of bytes per cluster
	int max_data_length;
	for (max_data_length = disk_size - 2, i = 1; max_data_length >= 0; max_data_length--, i++) {
		if (max_data_length * 2 <= cluster_size_bytes * i) {
			MBR->fat_length = disk_size - max_data_length - 1;
			MBR->data_start = MBR->fat_start + MBR->fat_length;
			MBR->data_length = max_data_length;
			break;
		}
	}
	printf("fat start %d\nfat length %d\ndata start %d\ndata length %d\n", MBR->fat_start, MBR->fat_length, MBR->data_start, MBR->data_length);

	// initialization operations
	// - initialize the file system by writing zeros to every byte
	// size of resulting file should be equal to sector_size * cluster_size * disk_size
	int disk_size_bytes = sector_size * cluster_size * disk_size;
	FILE *fs;
	fs = fopen("FileSystem.bin", "wb");
	uint8_t init_fs[disk_size_bytes]; 
	for (i=0; i < disk_size_bytes; i++) {
		init_fs[i] = 0xFF;
	}
	fwrite(init_fs, sizeof(uint8_t), disk_size_bytes, fs);	
	fclose(fs);

	fs = fopen("FileSystem.bin", "r+b");

	// write the MBR
	fwrite(MBR, sizeof(*MBR), 1, fs);
	

	// create the root directory
	// root directory information is held starting in cluster 2
	// update the FAT entry from 0xFFFF to 0xFFFe
	fseek(fs, sector_size*cluster_size, SEEK_SET);
	uint16_t allocate = htons(0xFFFE);
	fwrite(&allocate, sizeof(uint16_t), 1, fs);

	fseek(fs, sector_size*cluster_size*2, SEEK_SET);
	entry_t *root = (entry_t *)malloc(sizeof(entry_t));
	uint32_t time_stamp = date_format();
	root->entry_type = 1;
	root->creation_date = htons((time_stamp>>16) & 0xFFFF);
	root->creation_time = htons(time_stamp & 0xFFFF);
	char name[5] = "root";
	for (i=0; i<5; i++) {
		root->name[i] = name[i];
	}	
	  
	root->name_len = 4;
	root->size = 1; // a directory is always size 0	
	fwrite(root, sizeof(entry_t), 1, fs);	

	// finished initilizing the file system, close the file
	fclose(fs);	
	
	uint8_t test[disk_size_bytes];
	fs = fopen("FileSystem.bin", "r+b");
	fread(test, sizeof(uint8_t), disk_size_bytes, fs);
	fclose(fs);
	for(i=0; i<disk_size_bytes; i++) {
		printf("%d %d\n", i, test[i]);
	}
}

// load the disk into memory
void load_disk(char *disk_name) {
	// allocate memory for an mbr_t structure
	MBR_memory = (mbr_t *)malloc(sizeof(mbr_t));
	FILE *fs;
	fs = fopen(disk_name, "r+b");
	fseek(fs, 0, SEEK_SET);
	fread(MBR_memory, sizeof(mbr_t), 1, fs);

	uint16_t cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;
	// allocate memory for the FAT in memory
	FAT_memory = (uint16_t *)malloc(sizeof(uint16_t)*MBR_memory->data_length);
	fseek(fs, cluster_size_bytes, SEEK_SET);
	fread(FAT_memory, sizeof(uint16_t), MBR_memory->data_length, fs);
	
	// allocate memory for the Data area
	DATA_memory = malloc(sizeof(uint8_t) * MBR_memory->data_length * cluster_size_bytes);
	fseek(fs, cluster_size_bytes*MBR_memory->data_start , SEEK_SET);
	fread(DATA_memory, sizeof(uint8_t), cluster_size_bytes*MBR_memory->data_length, fs);

	fclose(fs);
}



int main(int argc, char *argv[]) {
	
	format(64, 1, 10);
	load_disk("FileSystem.bin");

	return 0;
}

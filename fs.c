#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

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
		init_fs[i] = 0;
	}
	fwrite(init_fs, sizeof(uint8_t), disk_size_bytes, fs);	
	fclose(fs);

	fs = fopen("FileSystem.bin", "r+b");

	// write the MBR
	fwrite(MBR, sizeof(*MBR), 1, fs);
	
	// initialize the FAT area by giving each entry the value 0xFFFF
	// create array of size data_length and fill each entry with 0xFFFF
	uint16_t init_FAT[MBR->data_length];
	for (i=0; i<MBR->data_length; i++) {
		init_FAT[i] = 0xFFFF;
	}
	// find the start of cluster 1
	fseek(fs, sector_size*cluster_size, SEEK_SET);
	fwrite(init_FAT, sizeof(uint16_t), MBR->data_length, fs);

	fclose(fs);	
	
	uint8_t test[disk_size_bytes];
	fs = fopen("FileSystem.bin", "r+b");
	fread(test, sizeof(uint8_t), disk_size_bytes, fs);
	fclose(fs);
	for(i=0; i<disk_size_bytes; i++) {
		printf("%d %d\n", i, test[i]);
	}
}

int main(int argc, char *argv[]) {
	
	format(64, 1, 20);


	return 0;
}

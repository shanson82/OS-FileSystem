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
		printf("%d %d\n", i, init_fs[i]);
	}
	fwrite(init_fs, sizeof(uint8_t), disk_size_bytes, fs);	
	fclose(fs);

	//FILE *fs;
	fs = fopen("FileSystem.bin", "r+b");
	// write the MBR
	fwrite(MBR, sizeof(*MBR), 1, fs);
	fclose(fs);	

	fs = fopen("FileSystem.bin", "r+b");
	mbr_t* y = (mbr_t *)malloc(sizeof(MBR));
	fread(y, sizeof(*MBR), 1, fs);
	fclose(fs);
	printf("fat start %d\nfat length %d\ndata start %d\ndata length %d\n",y->fat_start, y->fat_length, y->data_start,y->data_length);


	printf("sizeof mbr struct: %lu\n", sizeof(mbr_t));	
	printf("sizeof mbr struct: %lu\n", sizeof(*MBR));	

	fs = fopen("FileSystem.bin", "r+b");
	fseek(fs, sizeof(uint8_t) * cluster_size, SEEK_SET);
	uint8_t x[] = {5,4};
	fwrite(x, sizeof(x), sizeof(x)/sizeof(x[0]), fs);
	fclose(fs);
}

int main(int argc, char *argv[]) {
	
	format(64, 1, 20);


	return 0;
}

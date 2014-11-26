#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// structure to store Master Boot Record information
typedef struct mbr {
	uint16_t sector_size; // bytes ( >= 64 bytes)
	uint16_t cluster_size; // number of sectors (at least 1 sector/cluster) 
	uint16_t disk_size; // size of disk in clusters
	uint16_t fat_start;
	uint16_t fat_length; // number of clusters
	uint16_t data_start; 
	uint16_t data_length; // clusters
	char* disk_name[32];
} mbr_t;



void format(uint16_t sector_size, uint16_t cluster_size, uint16_t disk_size) {
	mbr_t *MBR = (mbr_t *)malloc(sizeof(mbr_t));
	MBR->sector_size = sector_size;
	MBR->cluster_size = cluster_size;
	MBR->disk_size = disk_size;
	MBR->fat_start = 1;

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
	
}

int main(int argc, char *argv[]) {
	
	format(64, 2, 40);


	return 0;
}

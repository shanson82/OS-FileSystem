#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h> // allows for use of htons()

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
char* DISK_NAME = "FileSystem.bin";

// structures and functions associated with linked lists
// linked list is used to store a path (parameter of fs_opendir)
// - note to self: maybe better suited in a different file
typedef struct node {
	char *dir;
	uint8_t cluster;
	struct node *next;
} node_t;

// insert at end of list
// e.g /root/OS/hw yields root->OS->hw->null
void insert(node_t **headRef, char* dir) {
	node_t *newNode = (node_t *)malloc(sizeof(node_t));
	newNode->dir = dir;
	//if (strcmp(dir, "root") == 0) {
	//	printf("passed strcmp test\n");
	//	newNode->cluster = 0;
	//}
	newNode->next = NULL;
	if (*headRef == NULL) {
		*headRef = newNode;
	} else {
		node_t *current = *headRef;
		while (current->next != NULL) {
			current = current->next;
		}
		current->next = newNode;
	}
}

void update_cluster(node_t **headRef, char* dir, uint8_t cluster) {
	
}

// given a directory, find the next directory in the list
char *get_next_dir(node_t **headRef, char* current_dir) {
	if (*headRef == NULL) return NULL;
	node_t *current = *headRef;
	while (current->next != NULL) {
		if (strcmp(current->dir, current_dir) == 0) {
			return current->next->dir;
		}
		current = current->next;
	}
	return NULL;
}

int get_length(node_t **headRef) {
	if (*headRef == NULL) return 0;
	node_t *current = *headRef;
	int count = 0;
	while (current->next != NULL) {
		count++;
		current = current->next;
	}
	return count;
}

// free the memory used by a list
void empty_list(node_t **headRef) {
	node_t *current = *headRef;
	if (current->next == NULL) {
		free(current);
	} else {
		while (current->next != NULL) {
			node_t *temp = current;
			current = current->next;
			free(temp);
		}
	}
	*headRef = NULL;
}

void print(node_t **headRef) {
	printf("printing list\n");
	node_t *current = *headRef;
	if (current == NULL) {
		printf("List is empty\n");
	} else {
		while (current->next != NULL) {
			printf("%s %d\n", current->dir, current->cluster);
			current = current->next;
		}
	}
}

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
	uint16_t allocate = 0xFFFE;
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

	/*	
	uint8_t test[disk_size_bytes];
	fs = fopen("FileSystem.bin", "r+b");
	fread(test, sizeof(uint8_t), disk_size_bytes, fs);
	fclose(fs);
	for(i=0; i<disk_size_bytes; i++) {
		printf("%d %d\n", i, test[i]);
	}
	*/
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

// fill entry struct
entry_t *fill_child_entry(int dh) {
	entry_t *child = malloc(sizeof(entry_t));
	int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;
	int lookup = (1 + MBR_memory->fat_length + dh) * cluster_size_bytes;
	FILE *fs;
	fs = fopen(DISK_NAME, "rb+");
	fseek(fs, lookup, SEEK_SET);
	fread(child, sizeof(entry_t), 1, fs);
	fclose(fs);
	return child; 	
	
}

// return a child, if any of a directory
entry_t *fs_ls(int dh, int child_num) {
	int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;
	int lookup = dh * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t); 	
	entry_ptr_t *ptr = malloc(sizeof(entry_ptr_t));
	ptr->type = DATA_memory[lookup];
	ptr->reserved = DATA_memory[lookup + 1];
	ptr->start = (DATA_memory[lookup + 2] << 8) + DATA_memory[lookup + 3];
	
	if (ptr->type != 0 || ptr->type != 1 || ptr->type != 2)
		return NULL;
	entry_t *child = fill_child_entry((int)ptr->start);
	return child;

}

// make a new directory where the parent is located at the data cluster indicated by dh
void fs_mkdir(int dh, char* child_name) {
	load_disk(DISK_NAME);

	// search FAT for the first open cluster
	int child_cluster;
	for (child_cluster=0; child_cluster<MBR_memory->data_length; child_cluster++) {
		printf("%d\n", FAT_memory[child_cluster]);
		if (FAT_memory[child_cluster] == 0xFFFF) {
			FAT_memory[child_cluster] = 0xFFFE;
			break;
		}
	}
	
	// create directory
	entry_t *child = (entry_t *)malloc(sizeof(entry_t));
	child->entry_type = 1; // directory
	uint32_t time_stamp = date_format();
	child->creation_date = htons((time_stamp>>16) & 0xFFFF);
	child->creation_time = htons(time_stamp & 0xFFFF);
	child->name_len = strlen(child_name);
	strcpy(child->name, child_name);
	child->size = 0; // size 0 for directories

	// create pointer to directory
	
	printf("next open spot: %d\n", child_cluster);
	printf("FAT_memory[%d] = %d\n", child_cluster, FAT_memory[child_cluster]);
	printf("child name %s\n", child->name);
	
	// write the new directory, the pointer to the new directory, and the updated FAT to the disk
	FILE *fs;
	fs = fopen(DISK_NAME, "rb+");
	int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;

	// new directory
	int child_location = (1 + MBR_memory->fat_length + child_cluster) * cluster_size_bytes;
	fseek(fs, child_location, SEEK_SET);
	fwrite(child, sizeof(entry_t), 1, fs);

	// pointer to the new directory
	entry_ptr_t *ptr_to_child = (entry_ptr_t *)malloc(sizeof(entry_ptr_t));
	ptr_to_child->type = 1;
	ptr_to_child->reserved = 0;
	ptr_to_child->start = child_cluster;
	// iterate through pointers of parent directory to find next open slot
	int offset = dh * cluster_size_bytes + (int)sizeof(entry_t);
	while(1) {
		if (DATA_memory[offset] == 0 || DATA_memory[offset] == 1) {
			offset = offset + (int)sizeof(entry_ptr_t);
		} else if (DATA_memory[offset] == 2) {
			// find next pointer here and change offset accordingly
		} else {
			fseek(fs, (1 + MBR_memory->fat_length) * cluster_size_bytes + offset, SEEK_SET);
			fwrite(ptr_to_child, sizeof(entry_ptr_t), 1, fs);
			break;
		}
	}

	// FAT area
	fseek(fs, cluster_size_bytes, SEEK_SET);
	int r = fwrite(FAT_memory, sizeof(uint16_t), MBR_memory->data_length, fs);
	printf("write %d\n", r);
	int n;
	for (n=0; n<MBR_memory->data_length; n++)
		printf("%d %d\n", n, FAT_memory[n]);

	fclose(fs);	

	free(MBR_memory);
	free(FAT_memory);
	free(DATA_memory);
}


// open a directory with the absolute path name
int fs_opendir(char *absolute_path) {
	load_disk(DISK_NAME);

	int m; 
	for (m=0; m<MBR_memory->data_length; m++) {
		printf("%d %d\n", m, FAT_memory[m]);
	}
	// while parsing the path given by absolute_path, add each directory name to a linked list
	node_t *root = NULL; // head pointer/root of linked list of directories in the path
	char *token; // each token is a directory
	printf("absolute path: %s\n", absolute_path);
	// check that path isn't empty
	if (strlen(absolute_path) == 0) {
		return -1;
	}
	token = strtok(absolute_path, "/");
	// if root is not first directory given in path, then return -1
	if (strcmp(token, "root") != 0) {
		return -1;
	}
	// start building the list, that will be checked farther down
	insert(&root, token);
	while (token != NULL) {
		printf("%s\n", token);
		token = strtok(NULL, "/");
		insert(&root, token);
	}
	print(&root);

	// start checking the linked list of directories
	if (get_length(&root) == 0) return -1; // this condition shouldn't ever pass, but inserted just in case
	if (get_length(&root) == 1) return 0; // if length of list of directories is 1, then only directory is "root"
	else {
		int dh_next;
		int dh_current = 0; // the cluster that the current directory is held
		char *dir_current = "root"; // current directory
		char *dir_next = get_next_dir(&root, dir_current); // next directory in the path
		printf("next dir = %s\n", dir_next);	
		int i;
		int path_length = get_length(&root);
		for (i=0; i<path_length-1; i++) {
			int child_num = 0; 
			printf("current and next dir = %s, %s\n", dir_current, dir_next);
			
			while (child_num < 10) {
				entry_t *child = fs_ls(dh_current, child_num);
				// no child present, or no child matches the directory being searched for, return -1
				if (child == NULL) {
					empty_list(&root);
					free(MBR_memory);
					free(FAT_memory);
					free(DATA_memory);
					return -1;
				}
				int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;
				// child found with matching name
				if (strcmp(child->name, dir_next) == 0) {
					int dh_p1, dh_p2;
					// pull the pointer location of the child from the data that is in memory
					dh_p1 = DATA_memory[dh_current * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t) + 2];
					dh_p2 = DATA_memory[dh_current * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t) + 3];
					dh_next = (dh_p1 << 8) + dh_p2;
					dh_current = dh_next; // update
					break;
				}
				child_num++;
			}

			dir_current = dir_next; // update current directory
			dir_next = get_next_dir(&root, dir_current); // find the next directory in path 
		
		}
		empty_list(&root);
		free(MBR_memory);
		free(FAT_memory);
		free(DATA_memory);
		
		return dh_current;
	}

	empty_list(&root);	
	free(MBR_memory);
	free(FAT_memory);
	free(DATA_memory);
	return -1;
}

void print_disk() {
	int disk_size_bytes = 640;
	FILE *fs;
	uint8_t test[disk_size_bytes];
	fs = fopen("FileSystem.bin", "r+b");
	fread(test, sizeof(uint8_t), disk_size_bytes, fs);
	fclose(fs);
	int i;
	for(i=0; i<disk_size_bytes; i++) {
		printf("%d %d\n", i, test[i]);
	}
}	

int main(int argc, char *argv[]) {
	
	format(64, 1, 10);
	char path[] = "root/";
	int dh = fs_opendir(path);
	printf("opendir %d\n", dh);
	fs_mkdir(0, "help");
	print_disk();
	return 0;
}

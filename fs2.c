#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h> // allows for use of htons()

#define DISK_NAME "FileSystem.bin"
// structure to store Master Boot Record information
typedef struct __attribute__ ((__packed__)) {
	uint16_t sector_size; // bytes ( >= 64 bytes)
	uint16_t cluster_size; // number of sectors (at least 1 sector/cluster) 
	uint16_t disk_size; // size of disk in clusters
	uint16_t fat_start;
	uint16_t fat_length; // number of clusters
	uint16_t data_start; 
	uint16_t data_length; // clusters
	char disk_name[32];
} mbr_t;

// structure to store directory or file
typedef struct __attribute__ ((__packed__)) {
	uint8_t entry_type;
	uint16_t creation_date;
	uint16_t creation_time;
	uint8_t name_len;
	char name[16];
	uint32_t size;
	uint16_t children_count; // keep track of the number of children
} entry_t;

// structure to store pointer to another directory or file
typedef struct __attribute__ ((__packed__)) {
	uint8_t type;
	uint8_t reserved;
	uint16_t start;
} entry_ptr_t;

// ****************************** global variables ***********************//
// variables filled when load_disk function is called
mbr_t *MBR_memory; 
uint16_t *FAT_memory;
uint8_t *DATA_memory;  
// **********************************************************************//

// ************************** linked list related functions *************//
// structures and functions associated with linked lists
// linked list is used to store a path (parameter of fs_opendir)
typedef struct node {
	char *dir;
	struct node *next;
} node_t;

// insert at end of list
// e.g /root/OS/hw yields root->OS->hw->null
void insert(node_t **headRef, char* dir) {
	node_t *newNode = (node_t *)malloc(sizeof(node_t));
	newNode->dir = dir;
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

// find length of the linked list
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
			printf("%s\n", current->dir);
			current = current->next;
		}
	}
}
// **************** end linked list functions *****************//

// format the date for creation_date and creation_time fields of entry_t struct
// pack the date into an unsigned 32 bit integer that is later split into two 16 bit integers
uint32_t date_format() {
	time_t t = time(NULL);
	struct tm *tptr = localtime(&t);
	uint32_t time_stamp;
	time_stamp = ((tptr->tm_year-80)<<25) + ((tptr->tm_mon+1)<<21) + ((tptr->tm_mday)<<16) + (tptr->tm_hour<<11) + (tptr->tm_min<<5) + ((tptr->tm_sec)%60)/2;
	return time_stamp;
}

// create a entry_t struct to initialize a directory
entry_t *create_directory_entry(char *dir_name) {
	entry_t *dir = (entry_t *)malloc(sizeof(entry_t));
	uint32_t time_stamp = date_format();
	dir->entry_type = 1;
	dir->creation_date = htons((time_stamp>>16) & 0xFFFF);
	dir->creation_time = htons(time_stamp & 0xFFFF);
	dir->name_len = strlen(dir_name);
	memset(dir->name, 0, 16);
	strcpy(dir->name, dir_name);		  
	dir->size = 0; // a directory is always size 0	
	dir->children_count = 0;
	return dir;
}

// create an entry_ptr_t struct
entry_ptr_t *create_ptr(int type, int child_cluster) {
	entry_ptr_t *ptr = (entry_ptr_t *)malloc(sizeof(entry_ptr_t));
	ptr->type = type;
	ptr->reserved = 0;
	ptr->start = child_cluster;
	return ptr;
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
	memset(MBR->disk_name, 0, 32);
	strcpy(MBR->disk_name, "A");

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
	// DIAGNOSTIC printing
	//printf("fat start %d\n", MBR->fat_start);
	//printf("fat length %d\n", MBR->fat_length);
	//printf("data start %d\n", MBR->data_start);
	//printf("data length %d\n", MBR->data_length);

	// initialization operations
	// - initialize the file system by writing zeros to every byte
	// size of resulting file should be equal to sector_size * cluster_size * disk_size
	int disk_size_bytes = sector_size * cluster_size * disk_size;
	FILE *fs;
	fs = fopen(DISK_NAME, "wb");
	uint8_t init_fs[disk_size_bytes]; 
	for (i=0; i < disk_size_bytes; i++) {
		init_fs[i] = 0xFF;
	}
	fwrite(init_fs, sizeof(uint8_t), disk_size_bytes, fs);	
	fclose(fs);

	fs = fopen(DISK_NAME, "rb+");

	// write the MBR
	fwrite(MBR, sizeof(mbr_t), 1, fs);
	
	// create the root directory
	// root directory information is held starting in cluster 2
	// update the FAT entry from 0xFFFF to 0xFFFe
	fseek(fs, sector_size*cluster_size, SEEK_SET);
	uint16_t allocate = 0xFFFE;
	fwrite(&allocate, sizeof(uint16_t), 1, fs);

	fseek(fs, sector_size*cluster_size*2, SEEK_SET);
	entry_t *root = create_directory_entry("root");
	fwrite(root, sizeof(entry_t), 1, fs);	

	// finished initilizing the file system, close the file
	fclose(fs);	

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

// fill entry struct from disk
entry_t *fill_entry (int dh) {
	entry_t *e = malloc(sizeof(entry_t));
	int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;
	int lookup = (1 + MBR_memory->fat_length + dh) * cluster_size_bytes;
	FILE *fs;
	fs = fopen(DISK_NAME, "rb+");
	fseek(fs, lookup, SEEK_SET);
	fread(e, sizeof(entry_t), 1, fs);
	fclose(fs);
	return e; 	
}

// return a child, if any of a directory
entry_t *fs_ls(int dh, int child_num) {
	int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;
	// int lookup = dh * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t); 
	int lookup = htons(dh) * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t); 
	printf("dh, cluster size, entry_t, child_num, entry_ptr_t: %d %d %d %d %d\n", htons(dh), cluster_size_bytes, (int)sizeof(entry_t), child_num, (int)sizeof(entry_ptr_t));
	printf("cluster size, start for look up: %d %d\n", cluster_size_bytes, lookup);	
	entry_ptr_t *ptr = malloc(sizeof(entry_ptr_t));
	ptr->type = DATA_memory[lookup];
	ptr->reserved = DATA_memory[lookup + 1];
	//ptr->start = (DATA_memory[lookup + 2] << 8) + DATA_memory[lookup + 3];
	ptr->start = (DATA_memory[lookup + 3] << 8) + DATA_memory[lookup + 2];
	printf("type, reserved, start %d %d %d\n", ptr->type, ptr->reserved, ptr->start);	
	if (ptr->type != 0 && ptr->type != 1 && ptr->type != 2)
		return NULL;
	if (ptr->type == 1) {
		entry_t *child = fill_entry((int)ptr->start);
		return child;
	} else if (ptr->type == 2) {
		return NULL;
	}		
	return NULL;
}

// find the next free cluster available, returns -1 if disk is full
int find_free_cluster() {
	int child_cluster;
	for (child_cluster=0; child_cluster < MBR_memory->data_length; child_cluster++) {
		printf("%d %d\n", child_cluster, FAT_memory[child_cluster]);
		if (FAT_memory[child_cluster] == 0xFFFF) {
			FAT_memory[child_cluster] = 0xFFFE;
			return child_cluster;
		}
	}
	return -1;	
}

// make a new directory where the parent is located at the data cluster indicated by dh
void fs_mkdir(int dh, char* child_name) {
	if (strlen(child_name) > 16) {
		printf("Directory \"%s\" not made: name of directory must not exceed 16 bytes\n", child_name);
		return;
	}

	load_disk(DISK_NAME);
	
	// create file pointer and open disk
	FILE *fs;
	fs = fopen(DISK_NAME, "rb+");
	int cluster_size_bytes = MBR_memory->sector_size * MBR_memory->cluster_size;

	// update children count of parent directory
	int parent_location = (1 + MBR_memory->fat_length + dh) * cluster_size_bytes;
	entry_t *parent = fill_entry(dh);
	printf("children count: %d\n", parent->children_count);
	parent->children_count++;
	fseek(fs, parent_location, SEEK_SET);
	fwrite(parent, sizeof(entry_t), 1, fs);
	// save the children count for later use, this is the count with the new child directory
	uint16_t children_count = parent->children_count;

	// create the child directory and write to disk
	// find the next available spot to write to disk
	int child_cluster = find_free_cluster();
	printf("child_cluster = %d\n", child_cluster);
	if (child_cluster == -1) {
		printf("fs_mkdir: directory not made\nno free space left on disk for new directory\n");
		return;
	}
	entry_t *child = create_directory_entry(child_name);
	int child_location = (1 + MBR_memory->fat_length + child_cluster) * cluster_size_bytes;
	fseek(fs, child_location, SEEK_SET);
	fwrite(child, sizeof(entry_t), 1, fs);

	// pointer to the new directory, 1 indicates pointer to a directory
	entry_ptr_t *ptr_to_child = create_ptr(1, child_cluster);

	// iterate through pointers of parent directory to find next open slot
	int max_children_initial_cluster = (cluster_size_bytes - sizeof(entry_t)) / sizeof(entry_ptr_t);
	int max_children_overflow_cluster = cluster_size_bytes / sizeof(entry_ptr_t);
	printf("initial overflow %d %d\n", max_children_initial_cluster, max_children_overflow_cluster);

	if (children_count < max_children_initial_cluster) {

		int ptr_offset = (1+MBR_memory->fat_length+dh) * cluster_size_bytes + (int)sizeof(entry_t) + (children_count-1)*sizeof(entry_ptr_t);
		fseek(fs, ptr_offset, SEEK_SET);
		fwrite(ptr_to_child, sizeof(entry_ptr_t), 1, fs);

	} else if (children_count == max_children_initial_cluster) {
		// find free area in FAT
		int ptr_overflow_cluster = find_free_cluster();
		printf("ptr overflow cluster %d\n", ptr_overflow_cluster);
		// create a link cluster to write in last available area
		entry_ptr_t *link_ptr = create_ptr(2, ptr_overflow_cluster);
		int	link_ptr_offset = (1 + MBR_memory->fat_length + dh) * cluster_size_bytes + (children_count-1)*sizeof(entry_ptr_t);
		
		fseek(fs, link_ptr_offset, SEEK_SET);
		fwrite(link_ptr, sizeof(entry_ptr_t), 1, fs);
		
		int ptr_offset = (1 + MBR_memory->fat_length + ptr_overflow_cluster) * cluster_size_bytes;
		fseek(fs, ptr_offset, SEEK_SET);
		fwrite(ptr_to_child, sizeof(entry_ptr_t), 1, fs);
		parent->children_count++;

	}
	

/*	while(1) {
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
*/

	// write the updated FAT area to disk
	fseek(fs, cluster_size_bytes, SEEK_SET);
	fwrite(FAT_memory, sizeof(uint16_t), MBR_memory->data_length, fs);

	fclose(fs);	

	// free up any allocated memory
	free(child);
	free(parent);
	free(ptr_to_child);
	free(MBR_memory);
	free(FAT_memory);
	free(DATA_memory);
}


// open a directory with the absolute path name
int fs_opendir(char *absolute_path) {
	load_disk(DISK_NAME);

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
		int i;
		int path_length = get_length(&root);
		for (i=0; i<path_length-1; i++) {
			int child_num = 0; 
			printf("current and next dir = %s, %s\n", dir_current, dir_next);
			
			//while (child_num < 10) {
			while(1) {
				printf("child_num = %d\n", child_num);
				entry_t *child = fs_ls(dh_current, child_num);
				// no child present, or no child matches the directory being searched for, return -1
				if (child == NULL) {
					printf("child returned was NULL\n");
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
					printf("dh_current, csb, child_num %d %d %d\n", dh_current, cluster_size_bytes, child_num);
					// pull the pointer location of the child from the data that is in memory
					dh_p1 = DATA_memory[htons(dh_current) * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t) + 2];
					dh_p2 = DATA_memory[htons(dh_current) * cluster_size_bytes + sizeof(entry_t) + child_num * sizeof(entry_ptr_t) + 3];
					dh_next = (dh_p1 << 8) + dh_p2;
					dh_current = dh_next; // update
					printf("child name, dh %s %d\n", child->name, htons(dh_current));
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
		printf("dh_current %d %d\n", dh_current, htons(dh_current));	
		return htons(dh_current);
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
		if (i%32 == 0) {
			printf("%d %d ", i, test[i]);
		} else if (i %32 == 31) {
			printf("%d\n", test[i]);
		} else {
			printf("%d ", test[i]);
		}
	}
}	

int main(int argc, char *argv[]) {
	
	format(64, 1, 10);
	char path[] = "root/";
	int dh = fs_opendir(path);
	printf("opendir %d\n", dh);
	fs_mkdir(dh, "help");
	char path2[] = "root/help";

	printf("*********** opendir root/help*****************\n");
	dh = fs_opendir(path2);
	printf("opendir root/help %d\n", dh);
	fs_mkdir(dh, "os");
	printf("*********** opendir root/help/os *****************\n");
	char path3[] = "root/help/os";
	printf("opendir root/help/os %d\n", fs_opendir(path3)); 
	
	
	printf("*********** opendir root/ *****************\n");
	char path4[] = "root/";
	dh = fs_opendir(path4);
	printf("opendir root/ %d\n", dh);
	fs_mkdir(dh, "aardvark"); 
	
	printf("*********** opendir root/help/ *****************\n");
	char path5[] = "root/help";
	dh = fs_opendir(path5);
	printf("opendir root/help %d\n", dh);
	fs_mkdir(dh, "fsa"); 
	fs_mkdir(dh, "abcdefghijklmnopqrstuv");
	print_disk();
	return 0;
}

#include "ext2.h"

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fsuid.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>


#define EXT2_OFFSET_SUPERBLOCK 1024
#define EXT2_INVALID_BLOCK_NUMBER ((uint32_t) -1)

// Global variable
group_desc_t * groups;

/* open_volume_file: Opens the specified file and reads the initial
   EXT2 data contained in the file, including the boot sector and
   group descriptor table.
   
   Parameters:
     filename: Name of the file containing the volume data.
   Returns:
     A pointer to a newly allocated volume_t data structure with all
     fields initialized according to the data in the volume file
     (including superblock and group descriptor table), or NULL if the
     file is invalid or data is missing, or if the file is not an EXT2
     volume file system (s_magic does not contain the correct value).
 */
volume_t *open_volume_file(const char *filename) {
  
  /* TO BE COMPLETED BY THE STUDENT */
  volume_t *volume = fopen(filename,"rd");
  volume->fd = open(filename, O_RDONLY);
  if (volume->fd == -1) {
      return NULL; // ERROR HANDLING
  }


  int superblk_res;
  superblk_res = pread(volume->fd, &volume->super, sizeof(superblock_t), EXT2_OFFSET_SUPERBLOCK);
  if (superblk_res == -1 || volume->super.s_magic != EXT2_SUPER_MAGIC) {
      return NULL; // ERROR HANDLING
  }

  struct stat buffer;
  if (fstat(volume->fd, &buffer) == -1) {
      return NULL; // ERROR HANDLING
  }
  volume->volume_size = buffer.st_size;
  volume->block_size = 1024 << volume->super.s_log_block_size;
  volume->num_groups = (volume->super.s_blocks_count-1) / volume->super.s_blocks_per_group + 1;


  int groupRes;
  groups = malloc(sizeof(group_desc_t));
  if (volume->block_size == 1024) {
    groupRes = pread(volume->fd, groups, sizeof(group_desc_t), volume->block_size*2);
  }
  else {
    groupRes = pread(volume->fd, groups, sizeof(group_desc_t), volume->block_size);
  }
  if (groupRes == -1){
      return NULL; // ERROR HANDLING
  }
  volume->groups = groups;
  free(groups);

  return volume;
}

/* close_volume_file: Frees and closes all resources used by a EXT2 volume.
   
   Parameters:
     volume: pointer to volume to be freed.
 */
void close_volume_file(volume_t *volume) {

  /* TO BE COMPLETED BY THE STUDENT */
  groups = NULL;
  free(groups);
  close(volume->fd);
}

/* read_block: Reads data from one or more blocks. Saves the resulting
   data in buffer 'buffer'. This function also supports sparse data,
   where a block number equal to 0 sets the value of the corresponding
   buffer to all zeros without reading a block from the volume.
   
   Parameters:
     volume: pointer to volume.
     block_no: Block number where start of data is located.
     offset: Offset from beginning of the block to start reading
             from. May be larger than a block size.
     size: Number of bytes to read. May be larger than a block size.
     buffer: Pointer to location where data is to be stored.

   Returns:
     In case of success, returns the number of bytes read from the
     disk. In case of error, returns -1.
 */
ssize_t read_block(volume_t *volume, uint32_t block_no, uint32_t offset, uint32_t size, void *buffer) {

  /* TO BE COMPLETED BY THE STUDENT */
  if (block_no == 0) {
    memset(buffer, 0, sizeof(*buffer));
    return 0;
  }

  uint32_t blockOffset = block_no * volume->block_size + offset;
  return (uint32_t)pread(volume->fd, buffer, size, blockOffset);
}

/* read_inode: Fills an inode data structure with the data from one
   inode in disk. Determines the block group number and index within
   the group from the inode number, then reads the inode from the
   inode table in the corresponding group. Saves the inode data in
   buffer 'buffer'.
   
   Parameters:
     volume: pointer to volume.
     inode_no: Number of the inode to read from disk.
     buffer: Pointer to location where data is to be stored.

   Returns:
     In case of success, returns a positive value. In case of error,
     returns -1.
 */
ssize_t read_inode(volume_t *volume, uint32_t inode_no, inode_t *buffer) {
  
  /* TO BE COMPLETED BY THE STUDENT */
  if (inode_no == 0) {
    return read_block(volume,inode_no,0,0,buffer);
  }

  uint32_t blockGroup = (inode_no - 1) / volume->super.s_inodes_per_group;
  uint32_t index = (inode_no - 1) % volume->super.s_inodes_per_group;
  uint32_t offset = index * sizeof(uint32_t);
  //uint32_t containing_block = (index * volume->super.s_inode_size) / volume->block_size;
  return read_block(volume, volume->groups[blockGroup].bg_inode_table, offset, sizeof(inode_t), (void *)buffer);
}

/* read_ind_block_entry: Reads one entry from an indirect
   block. Returns the block number found in the corresponding entry.
   
   Parameters:
     volume: pointer to volume.
     ind_block_no: Block number for indirect block.
     index: Index of the entry to read from indirect block.

   Returns:
     In case of success, returns the block number found at the
     corresponding entry. In case of error, returns
     EXT2_INVALID_BLOCK_NUMBER.
 */
static uint32_t read_ind_block_entry(volume_t *volume, uint32_t ind_block_no, uint32_t index) {
  
  /* TO BE COMPLETED BY THE STUDENT */
  if (ind_block_no > volume->super.s_blocks_count)
    return EXT2_INVALID_BLOCK_NUMBER;
  uint32_t offset = index * sizeof(u_int32_t);
  uint32_t *temp;
  int result = read_block(volume, ind_block_no, offset, sizeof(u_int32_t), (void *)temp);
  if (result == -1)
    return EXT2_INVALID_BLOCK_NUMBER;
  return *temp;
}

/* read_inode_block_no: Returns the block number containing the data
   associated to a particular index. For indices 0-11, returns the
   direct block number; for larger indices, returns the block number
   at the corresponding indirect block.
   
   Parameters:
     volume: Pointer to volume.
     inode: Pointer to inode structure where data is to be sourced.
     index: Index to the block number to be searched.

   Returns:
     In case of success, returns the block number to be used for the
     corresponding entry. This block number may be 0 (zero) in case of
     sparse files. In case of error, returns
     EXT2_INVALID_BLOCK_NUMBER.
 */
static uint32_t get_inode_block_no(volume_t *volume, inode_t *inode, uint64_t block_idx) {
  
  /* TO BE COMPLETED BY THE STUDENT */
  if (block_idx>=0 && block_idx<=11) {
    return inode->i_block[block_idx];
    }
  u_int32_t size_1ind = volume->block_size / sizeof(u_int32_t);
  if (block_idx>11 && block_idx<size_1ind) {
    return read_ind_block_entry(volume, inode->i_block_1ind, block_idx-12);
  }
  u_int32_t size_2ind = size_1ind * volume->block_size;
  if (block_idx>=size_1ind && block_idx<size_2ind) {
    return read_ind_block_entry(volume, inode->i_block_2ind, block_idx-size_1ind-12);
  }
  u_int32_t size_3ind = size_1ind * size_1ind * volume->block_size;
  if (block_idx>=size_2ind && block_idx<size_3ind) {
    return read_ind_block_entry(volume, inode->i_block_3ind, block_idx-size_2ind-size_1ind-12);
  }
  return EXT2_INVALID_BLOCK_NUMBER;
}

/* read_file_block: Returns the content of a specific file, limited to
   a single block.
   
   Parameters:
     volume: Pointer to volume.
     inode: Pointer to inode structure for the file.
     offset: Offset, in bytes from the start of the file, of the data
             to be read.
     max_size: Maximum number of bytes to read from the block.
     buffer: Pointer to location where data is to be stored.

   Returns:
     In case of success, returns the number of bytes read from the
     disk. In case of error, returns -1.
 */
ssize_t read_file_block(volume_t *volume, inode_t *inode, uint64_t offset, uint64_t max_size, void *buffer) {
    
  /* TO BE COMPLETED BY THE STUDENT */
  uint64_t size = volume->block_size - (offset % volume->block_size);
  if (size > max_size) {
    size = max_size;
  }
  uint32_t block_no = get_inode_block_no(volume, inode, offset / volume->block_size);
  return read_block(volume, block_no, offset % volume->block_size, size, buffer);


}

/* read_file_content: Returns the content of a specific file, limited
   to the size of the file only. May need to read more than one block,
   with data not necessarily stored in contiguous blocks.
   
   Parameters:
     volume: Pointer to volume.
     inode: Pointer to inode structure for the file.
     offset: Offset, in bytes from the start of the file, of the data
             to be read.
     max_size: Maximum number of bytes to read from the file.
     buffer: Pointer to location where data is to be stored.

   Returns:
     In case of success, returns the number of bytes read from the
     disk. In case of error, returns -1.
 */
ssize_t read_file_content(volume_t *volume, inode_t *inode, uint64_t offset, uint64_t max_size, void *buffer) {

  uint32_t read_so_far = 0;

  if (offset + max_size > inode_file_size(volume, inode))
    max_size = inode_file_size(volume, inode) - offset;
  
  while (read_so_far < max_size) {
    int rv = read_file_block(volume, inode, offset + read_so_far,
			     max_size - read_so_far, buffer + read_so_far);
    if (rv <= 0) return rv;
    read_so_far += rv;
  }
  return read_so_far;
}

/* follow_directory_entries: Reads all entries in a directory, calling
   function 'f' for each entry in the directory. Stops when the
   function returns a non-zero value, or when all entries have been
   traversed.
   
   Parameters:
     volume: Pointer to volume.
     inode: Pointer to inode structure for the directory.
     context: This pointer is passed as an argument to function 'f'
              unmodified.
     buffer: If function 'f' returns non-zero for any file, and this
             pointer is set to a non-NULL value, this buffer is set to
             the directory entry for which the function returned a
             non-zero value. If the pointer is NULL, nothing is
             saved. If none of the existing entries returns non-zero
             for 'f', the value of this buffer is unspecified.
     f: Function to be called for each directory entry. Receives three
        arguments: the file name as a NULL-terminated string, the
        inode number, and the context argument above.

   Returns:
     If the function 'f' returns non-zero for any directory entry,
     returns the inode number for the corresponding entry. If the
     function returns zero for all entries, or the inode is not a
     directory, or there is an error reading the directory data,
     returns 0 (zero).
 */
uint32_t follow_directory_entries(volume_t *volume, inode_t *inode, void *context,
				  dir_entry_t *buffer,
				  int (*f)(const char *name, uint32_t inode_no, void *context)) {

  /* TO BE COMPLETED BY THE STUDENT */

  int offset = 0;
  dir_entry_t * temp = malloc(sizeof(dir_entry_t));
  int f_output = 0;
    printf("DEBUG INODE FILE SIZE: %ld \n", inode_file_size(volume,inode));
  while (offset < inode_file_size(volume,inode) && f==0) {
    printf("DEBUG DE WHILE LOOP \n");
      if (read_file_content(volume,inode,offset, sizeof(dir_entry_t),temp) <0) {
          free(temp);
          return 0;
      }

      char * tempName = malloc((temp->de_name_len + 2) * sizeof(char));
      strcpy(tempName, temp->de_name);
      tempName[temp->de_name_len] = "\0";
      f_output = (*f)(tempName,temp->de_inode_no,context);
      free(tempName);
      if (f_output != 0) {
          if (buffer!=NULL ) {
              read_file_content(volume,inode,offset, sizeof(dir_entry_t),temp);
          }
          int de_inode_no = temp->de_inode_no;
          free(temp);
          return de_inode_no;
      }
      offset += temp->de_rec_len;
  }

    free (temp);
  return 0;
}

/* Simple comparing function to be used as argument in find_file_in_directory function */
static int compare_file_name(const char *name, uint32_t inode_no, void *context) {
  return !strcmp(name, (char *) context);
}

/* find_file_in_directory: Searches for a file in a directory.
   
   Parameters:
     volume: Pointer to volume.
     inode: Pointer to inode structure for the directory.
     name: NULL-terminated string for the name of the file. The file
           name must match this name exactly, including case.
     buffer: If the file is found, and this pointer is set to a
             non-NULL value, this buffer is set to the directory entry
             of the file. If the pointer is NULL, nothing is saved. If
             the file is not found, the value of this buffer is
             unspecified.

   Returns:
     If the file exists in the directory, returns the inode number
     associated to the file. If the file does not exist, or the inode
     is not a directory, or there is an error reading the directory
     data, returns 0 (zero).
 */
uint32_t find_file_in_directory(volume_t *volume, inode_t *inode, const char *name, dir_entry_t *buffer) {
  return follow_directory_entries(volume, inode, (char *) name, buffer, compare_file_name);
}

/* find_file_from_path: Searches for a file based on its full path.
   
   Parameters:
     volume: Pointer to volume.
     path: NULL-terminated string for the full absolute path of the
           file. Must start with '/' character. Path components
           (subdirectories) must be delimited by '/'. The root
           directory can be obtained with the string "/".
     dest_inode: If the file is found, and this pointer is set to a
                 non-NULL value, this buffer is set to the inode of
                 the file. If the pointer is NULL, nothing is
                 saved. If the file is not found, the value of this
                 buffer is unspecified.

   Returns:
     If the file exists, returns the inode number associated to the
     file. If the file does not exist, or there is an error reading
     any directory or inode in the path, returns 0 (zero).
 */
uint32_t find_file_from_path(volume_t *volume, const char *path, inode_t *dest_inode) {

  /* TO BE COMPLETED BY THE STUDENT */
  printf("DEBUG AT THE BEGINING \n");

  // check if the path starts with "/"
  if (strncmp(path,"/",1)!=0){
        printf("DEBUG not / \n");
        return 0;
  }

  // for root directory
  if (strcmp(path,"/")==0) {
      if (read_inode(volume,EXT2_ROOT_INO,dest_inode) < 0) {
          return 0; //ERROR with read_inode
      } else {
        printf("DEBUG: SUCCESSFULLY READ INODE FOR ROOT \n");
          return EXT2_ROOT_INO;
      }
  }

  char delim[] = "/";
  char *ptr = strtok(&path,delim);
  char *nextptr;
  int inode_no;
  inode_t * curr_inode = malloc(sizeof(inode_t));
  read_inode(volume,EXT2_ROOT_INO,curr_inode);
  printf("DEBUG BEFORE WHILE LOOP \n");

  while (ptr != NULL) {
      printf("DEBUG INSIDE WHILE LOOP \n");
      nextptr = strtok(NULL, delim); // test the next address

      
      if (nextptr == NULL){ // if this is the last address
        inode_no = find_file_in_directory(volume,curr_inode, ptr, NULL); //TODO SHOULD IT BE NULL?
        if (inode_no == 0) {
            printf("DEBUG ERROR with find_file_in_directory \n");
            free(curr_inode);
            return 0; // ERROR with find_file_in_directory
        }
        if (read_inode(volume,inode_no,dest_inode) < 0) {
            printf("DEBUG ERROR with read_inode \n");
            free(curr_inode);
            return 0; // ERROR with read_inode
        }
        free(curr_inode);
        return inode_no;
      } else { // what if it's not the last address yet?
        char * curr_name = malloc(sizeof(char) * (strlen(ptr)+2));
        strcpy(curr_name,ptr);
        curr_name[sizeof(char)*strlen(ptr)] = "\0";
        if (find_file_in_directory(volume,curr_inode, curr_name, NULL) <= 0) {
          free(curr_name);
          free(curr_inode);
          return 0;
        }
        if (read_inode(volume,inode_no,curr_inode ) < 0 ){
          free(curr_inode);
          free(curr_name);
          return 0;
        }
        free(curr_name);
      }

      
      // next iteration
      ptr = nextptr;
  }


  printf("DEBUG END \n");
  free(curr_inode);
  return 0; // NEED TO REPLACE
}

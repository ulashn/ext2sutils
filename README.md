# ext2sutils

This is an extenden version of the ext2 Linux filesystem which has two more functionalities.

This code was shared for showing how can we manipulate the filesystem and how can we create the new one where we can use for our needs.

### Dup

- In this functionality we can duplicate the only inode of the file.
- This gives us more space for saving files but if one of them is changed the other one is effected. :)


### Rm

- This is same as normal `$ rm` command in Linux.
- In ext2s this will also check the sharing inodes of the files which will removed.


## Usage

**For dup:**

- `$ ./ext2sutils dup FS_IMAGE SOURCE DEST`
- `FS_IMAGE` will be a valid ext2s filesystem image.
- `SOURCE` and `DEST` always refers to regular files.

Possible `SOURCE` formats:
1. inode_no: Just a valid inode number. With this format, `SOURCE` is the inode number of the file to
be duplicated.
2. /abs/path/to/filename: An absolute path to the file starting from the root directory /. To simplify
your implementation, the separator is guaranteed to be a single slash, there will never be multiple
consecutive slashes.

Possible `DEST` formats:
1. dir_inode/target_name: The left side of the / is a directory inode, and the right side is the name
of the destination duplicated file.
2. /abs/path/to/target_name: An absolute path, same properties as with `SOURCE`. The last compo-
nent of the path is always the name of destination file and never just a directory. Intermediate
directories in the path will always exist.

Example Runs: 

- `$ ./ext2sutils dup fs.img 15 2/file.txt`
- `$ ./ext2sutils dup myext2.img /folder/code1.c 14/code.c`
- `$ ./ext2sutils dup example.img 88 /home/torag/docs/recovered.bin`
- `$ ./ext2sutils dup example.img /y/2020/hr.avi /y/2021/nostalgia.avi`

**For rm:**

- `./ext2sutils rm FS_IMAGE DEST`
- `FS_IMAGE` will be a valid ext2s filesystem image.
- `SOURCE` and `DEST` always refers to regular files same as dup they have possible formats.


Example Runs: 

- `$ ./ext2sutils rm fs.img 17/useless.bin`
- `$ ./ext2sutils rm myfs.img /secret/very_hidden/.sn34ky/keys.txt`


#include <iostream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "ext2fs.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bitset>
#include <time.h>

#define MAX_DIRECTORY_SIZE 264

using namespace std;

ext2_super_block superBlock;
refctr_t referenceCounter;
vector<ext2_block_group_descriptor *> descs;
ext2_inode rootNode;
static unsigned int blockSize = 0;
static unsigned int groupCount = 0;
static unsigned int descSize = 0;
static unsigned int inodeSize = 0;
vector<char *> sourcePath;
vector<char *> destinationPath;
#define OFFSET(n) (EXT2_BOOT_BLOCK_SIZE + ((n - 1) * blockSize))
#define OFFSET2(n) (n * blockSize)
#define BLOCK_SIZE(n) (n)
vector<unsigned int> removedBlocks;

unsigned char *bitMap;

int imageFD;

unsigned int getTime()
{
    unsigned int current = (unsigned int)time(NULL);
    if (((time_t)current) == ((time_t)-1))
    {
        exit(1);
    }
    return current;
}

unsigned int round4(int num)
{
    unsigned int result = 0;
    while (result < num)
    {
        result += 4;
    }
    return result;
}

void printDestinationPath()
{
    for (int i = 0; i < destinationPath.size(); i++)
    {
        cout << i + 1 << ": " << destinationPath[i] << endl;
    }
}

void printSourcePath()
{
    for (int i = 0; i < sourcePath.size(); i++)
    {
        cout << i + 1 << ": " << sourcePath[i] << endl;
    }
}

void printSuperBlock(ext2_super_block sb)
{
    cout << "--------SUPER BLOCK---------" << endl;
    cout << "Total space: " << blockSize * sb.block_count << endl;
    cout << "Total Block count: " << sb.block_count << endl;
    cout << "Total Inode count: " << sb.inode_count << endl;
    cout << "Total free blocks: " << sb.free_block_count << endl;
    cout << "Total free inodes: " << sb.free_inode_count << endl;
    cout << "Blocks/group: " << sb.blocks_per_group << endl;
    cout << "Inode/group: " << sb.inodes_per_group << endl;
    cout << "Group count: " << groupCount << endl;
    cout << "Inode Size: " << sb.inode_size << endl;
    //cout << "First inode: " << sb.first_inode << endl;
}

void printGroupDesc(ext2_block_group_descriptor *gd)
{
    cout << "--------GROUP DESCRIPTOR TABLE---------" << endl;
    cout << "Blocks bitmap block: " << gd->block_bitmap << endl;
    cout << "Inodes bitmap block: " << gd->inode_bitmap << endl;
    cout << "Inodes Table block: " << gd->inode_table << endl;
    cout << "Free Blocks count: " << gd->free_block_count << endl;
    cout << "Free Inodes count: " << gd->free_inode_count << endl;
    cout << "Directories count: " << gd->used_dirs_count << endl;
    cout << "Refmap block: " << gd->block_refmap << endl;
}

void printInode(ext2_inode *temp)
{
    cout << "--------INODE---------" << endl;
    cout << "Mode: " << temp->mode << endl;
    cout << "Users: " << temp->uid << endl;
    cout << "Size: " << temp->size << endl;
    cout << "Groups: " << temp->gid << endl;
    cout << "Link count: " << temp->link_count << endl;
    cout << "Block count: " << temp->block_count_512 << endl;
    if (S_ISDIR(temp->mode))
    {
        cout << "This is a directory" << endl;
    }
}

ext2_inode *copyInode(const ext2_inode *rhs)
{
    ext2_inode *newnode = new ext2_inode;
    newnode->mode = rhs->mode;
    newnode->uid = rhs->uid;
    newnode->size = rhs->size;
    unsigned int now = getTime();
    newnode->access_time = rhs->access_time;
    newnode->creation_time = now;
    newnode->modification_time = now;
    newnode->deletion_time = 0;
    newnode->link_count = rhs->link_count;
    newnode->block_count_512 = rhs->block_count_512;
    newnode->flags = rhs->flags;
    newnode->reserved = rhs->reserved;
    newnode->single_indirect = rhs->single_indirect;
    newnode->double_indirect = rhs->double_indirect;
    newnode->triple_indirect = rhs->triple_indirect;
    //newnode->direct_blocks = new uint32_t[12];
    //cout << "Copy Inode" << endl;
    for (int i = 0; i < 7; i++)
    {
        /*
        if (rhs->padding[i])
        {
            newnode->padding[i] = 0;
            //cout << "?" << endl;
            continue;
        }*/
        newnode->padding[i] = rhs->padding[i];
    }
    for (int j = 0; j < 12; j++)
    {
        /*
        if (rhs->direct_blocks[j])
        {
            newnode->direct_blocks[j] = 0;
            //cout << "???" << endl;
            continue;
        }*/
        //cout << "Direct block " << j << ": " << rhs->direct_blocks[j] << endl;
        newnode->direct_blocks[j] = rhs->direct_blocks[j];
    }
    //cout << "Copy Inode Sonu" << endl;
    return newnode;
}

bool readSuperBlock(ext2_super_block *sb, int fd)
{

    // Always at 1024. byte

    lseek(fd, EXT2_SUPER_BLOCK_POSITION, SEEK_SET);
    read(fd, sb, sizeof(*sb));

    // Checking the magic number

    if (sb->magic != EXT2_SUPER_MAGIC)
    {
        cerr << "Image is not ext2 typed filesystem." << endl;
        return false;
    }

    // Get block size from the 2^(10 + num) = blocksize
    // Set the global variables for later usage.
    blockSize = EXT2_UNLOG(sb->log_block_size);
    bitMap = new unsigned char[blockSize];
    groupCount = 1 + ((superBlock.inode_count - 1) / superBlock.inodes_per_group);
    descSize = groupCount * blockSize;
    inodeSize = superBlock.inode_size;

    // Properly read
    return true;
}

void writeSuperBlock(ext2_super_block *sb, int fd)
{
    lseek(fd, EXT2_SUPER_BLOCK_POSITION, SEEK_SET);
    write(fd, sb, sizeof(*sb));
}

void readGroupDescriptor(int fd, ext2_block_group_descriptor *gd, int groupNum)
{
    /*
        We have group descriptor table after the Superblock where:
        - BS = 1024 -> Block 2 --------- BS > 1024 -> Block 1
        - We are reading the groupNum * sizeof(groupDescriptor)
        - They are sequentially stored on this table.
    */

    if (blockSize == 1024)
    {
        lseek(fd, OFFSET(2) + (sizeof(*gd) * groupNum), SEEK_SET);
    }
    else
    {
        lseek(fd, OFFSET2(1) + (sizeof(*gd) * groupNum), SEEK_SET);
    }
    read(fd, gd, sizeof(*gd));
}

void writeGroupDescriptor(int fd, ext2_block_group_descriptor *gd, int groupNum)
{
    if (blockSize == 1024)
    {
        lseek(fd, OFFSET(2) + (sizeof(*gd) * groupNum), SEEK_SET);
    }
    else
    {
        lseek(fd, OFFSET2(1) + (sizeof(*gd) * groupNum), SEEK_SET);
    }
    write(fd, gd, sizeof(*gd));
}

void readInode(int fd, int inodeNumber, ext2_inode *temp)
{
    /**
	 * Read given Inode number and fill the temp pointer with this inode struct
	*/
    int whichGroup = (inodeNumber - 1) / superBlock.inodes_per_group;
    int localPos = (inodeNumber - 1) % superBlock.inodes_per_group;
    //cout << "Which group: " << whichGroup << endl;
    //cout << "Group Position:" << localPos << endl;
    //printGroupDesc(descs[whichGroup]);
    if (blockSize == 1024)
    {
        lseek(fd, OFFSET(descs[whichGroup]->inode_table) + (localPos * sizeof(struct ext2_inode)), SEEK_SET);
    }
    else
    {
        lseek(fd, OFFSET2(descs[whichGroup]->inode_table) + (localPos * sizeof(struct ext2_inode)), SEEK_SET);
    }
    read(fd, temp, sizeof(struct ext2_inode));
}

void writeInode(int fd, int inodeNumber, ext2_inode *temp)
{
    /**
	 * Write temp pointer data to given inode number
	*/
    //cout << "Write ici" << endl;
    //cout << superBlock.inodes_per_group << endl;
    //cout << inodeNumber << endl;
    int whichGroup = (inodeNumber - 1) / superBlock.inodes_per_group;
    int localPos = (inodeNumber - 1) % superBlock.inodes_per_group;
    //cout << "WhichGroup: " << whichGroup << endl;
    //cout << "LocalPos: " << localPos << endl;
    if (blockSize == 1024)
    {
        lseek(fd, OFFSET(descs[whichGroup]->inode_table) + (localPos * sizeof(ext2_inode)), SEEK_SET);
    }
    else
    {
        lseek(fd, OFFSET2(descs[whichGroup]->inode_table) + (localPos * sizeof(ext2_inode)), SEEK_SET);
    }
    //cout << "Tam yazmadan once." << endl;
    write(fd, temp, sizeof(ext2_inode));
}

void fillBitmap(int groupNo, unsigned char bitmap[])
{
    if (groupNo == 0)
    {
        lseek(imageFD, EXT2_GROUP_DESCR_POSITION + ((descs[groupNo]->block_bitmap - 1) * blockSize), SEEK_SET);
        read(imageFD, bitmap, blockSize);
    }
    else
    {
        lseek(imageFD, (EXT2_BOOT_BLOCK_SIZE + (superBlock.blocks_per_group * blockSize * groupNo)) + ((descs[groupNo]->block_bitmap - 1) * blockSize), SEEK_SET);
        read(imageFD, bitmap, blockSize);
    }
}

/**
 * Avaliable bitmaps:
 * - Bulunan inode numarası = (groupcount)*(inode_per_group)+(i+1)
*/

void printBitMap(unsigned char bitmap[], unsigned int size)
{
    for (int i = 0; i < size; i++)
    {
        unsigned char temp = bitmap[i];
        for (int j = 0; j < 8; j++)
        {
            cout << ((temp >> j) & 1) << " ";
        }
    }
}

/**
 * This this function will search directory entry with given name and returns the inode_num of it.
*/

int getInodeOfGivenEntryName(char *filename, ext2_inode *searched)
{

    // First we split the path to tokens which are directory entries.

    unsigned char blockData[blockSize];
    for (int i = 0; i < 12; i++)
    {
        if (blockSize == 1024)
        {
            lseek(imageFD, OFFSET(searched->direct_blocks[i]), SEEK_SET);
        }
        else
        {
            lseek(imageFD, OFFSET2(searched->direct_blocks[i]), SEEK_SET);
        }
        read(imageFD, blockData, blockSize);
        int currentPosition = 0;
        ext2_dir_entry *thisDir = (ext2_dir_entry *)blockData;
        //cout << "Length: " << thisDir->length << endl;
        //cout << "NameLength: " << unsigned(thisDir->name_length) << endl;
        //cout << "InodeNum: " << thisDir->inode << endl;
        while (currentPosition < blockSize)
        {
            // We are searching through the block data
            // If it will exceed we will search the next direct block of inode.
            char entryFilename[EXT2_MAX_NAME_LENGTH + 1];
            memcpy(entryFilename, thisDir->name, unsigned(thisDir->name_length));
            //cout << "Enrty Filename: " << entryFilename << endl;
            entryFilename[unsigned(thisDir->name_length)] = '\0';
            //cout << "Gezilen: " << entryFilename << endl;
            // Compare the filenames if they are equal we are done!
            if (strcmp(entryFilename, filename) == 0)
            {
                return thisDir->inode;
                // We found the file and if it is the last filename of this path we are done!
            }
            currentPosition += thisDir->length;
            thisDir = (ext2_dir_entry *)((char *)thisDir + thisDir->length);
        }
    }
    return -1;
}

unsigned int getFirstFreeBlock()
{
    if (superBlock.free_block_count != 0)
    {
        for (int index = 0; index < descs.size(); index++)
        {
            char blockBMap[blockSize];
            if (blockSize == 1024)
            {
                lseek(imageFD, OFFSET(descs[index]->block_bitmap), SEEK_SET);
            }
            else
            {
                lseek(imageFD, OFFSET2(descs[index]->block_bitmap), SEEK_SET);
            }
            read(imageFD, &blockBMap, blockSize);
            int i = 0;
            int j = 0;
            while (i < superBlock.block_count / 8)
            {
                while (j < 8)
                {
                    if ((int)((blockBMap[i] >> j) & 1) == 0)
                    {
                        break;
                    }
                    j++;
                }
                if (j < 8)
                {
                    break;
                }
                j = 0;
                i++;
            }
            return (i * 8) + j + (index * superBlock.blocks_per_group);
        }
        return 0;
    }
    return 0;
}

unsigned int getFirstFreeInode()
{
    if (superBlock.free_inode_count != 0)
    {
        for (int index = 0; index < descs.size(); index++)
        {
            char inodeBMap[blockSize];
            if (blockSize == 1024)
            {
                lseek(imageFD, OFFSET(descs[index]->inode_bitmap), SEEK_SET);
            }
            else
            {
                lseek(imageFD, OFFSET2(descs[index]->inode_bitmap), SEEK_SET);
            }
            read(imageFD, &inodeBMap, blockSize);
            int i = 1;
            int j = (superBlock.first_inode - 1) % 8;
            while (i < superBlock.inode_count / 8)
            {
                while (j < 8)
                {
                    if ((int)((inodeBMap[i] >> j) & 1) == 0)
                    {
                        break;
                    }
                    j++;
                }
                if (j < 8)
                {
                    break;
                }
                j = 0;
                i++;
            }
            return (i * 8) + j + 1 + (index * superBlock.inodes_per_group);
        }
        return 0;
    }
    return 0;
}

void incrementBlockRefMap(int blockNum)
{
    int whichGroup = (blockNum) / superBlock.blocks_per_group;
    int localPos = (blockNum) % superBlock.blocks_per_group;
    int refmapStart = descs[whichGroup]->block_refmap;
    //cout << "RefmapBlock: " << refmapStart << endl;
    //cout << "LocalPos:" << localPos << endl;
    uint32_t refMapBlocks[blockSize * 8];
    //cout << "Refmap olusturuldu" << endl;
    //cout << "Which group: " << whichGroup << endl;
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(refmapStart), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(refmapStart), SEEK_SET);
    }
    read(imageFD, refMapBlocks, 32 * blockSize);

    refMapBlocks[localPos]++;

    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(refmapStart), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(refmapStart), SEEK_SET);
    }
    write(imageFD, refMapBlocks, 32 * blockSize);
    //cout << "Refmap Sonu" << endl;
    //delete refMapBlocks;
}

unsigned int decrementBlockRefMap(int blockNum)
{
    int whichGroup = (blockNum) / superBlock.blocks_per_group;
    int localPos = (blockNum) % superBlock.blocks_per_group;
    int refmapStart = descs[whichGroup]->block_refmap;
    unsigned int result;
    uint32_t refMapBlocks[blockSize * 8];
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(refmapStart), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(refmapStart), SEEK_SET);
    }
    read(imageFD, refMapBlocks, 32 * blockSize);
    if (refMapBlocks[localPos] > 0)
    {
        refMapBlocks[localPos]--;
        result = refMapBlocks[localPos];
    }
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(refmapStart), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(refmapStart), SEEK_SET);
    }
    write(imageFD, refMapBlocks, 32 * blockSize);
    //delete refMapBlocks;
    return result;
}

void deallocateBlockBitMap(int blockNum)
{
    superBlock.free_block_count += 1;
    int whichGroup = (blockNum) / superBlock.blocks_per_group;
    int localPos = (blockNum) % superBlock.blocks_per_group;
    descs[whichGroup]->free_block_count++;
    int bitmapBlock = descs[whichGroup]->block_bitmap;
    unsigned char blockBMap[blockSize];
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapBlock), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapBlock), SEEK_SET);
    }
    read(imageFD, blockBMap, blockSize);
    unsigned char bytePos = blockBMap[(blockNum - 1) / 8];
    int bitPos = localPos % 8;
    blockBMap[localPos / 8] = bytePos & ~(1 << bitPos);
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapBlock), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapBlock), SEEK_SET);
    }
    write(imageFD, blockBMap, blockSize);
    writeSuperBlock(&superBlock, imageFD);
    writeGroupDescriptor(imageFD, descs[whichGroup], whichGroup);
    //delete bitmapBlock;
}

void allocateBlockBitMap(int blockNum)
{
    superBlock.free_block_count -= 1;
    int whichGroup = (blockNum) / superBlock.blocks_per_group;
    int localPos = (blockNum) % superBlock.blocks_per_group;
    descs[whichGroup]->free_block_count--;
    int bitmapBlock = descs[whichGroup]->block_bitmap;
    unsigned char blockBMap[blockSize];
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapBlock), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapBlock), SEEK_SET);
    }
    read(imageFD, blockBMap, blockSize);
    unsigned char bytePos = blockBMap[(blockNum - 1) / 8];
    int bitPos = (localPos) % 8;
    blockBMap[(localPos) / 8] = bytePos | (1 << bitPos);
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapBlock), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapBlock), SEEK_SET);
    }
    write(imageFD, blockBMap, blockSize);
    writeSuperBlock(&superBlock, imageFD);
    writeGroupDescriptor(imageFD, descs[whichGroup], whichGroup);
}

void deallocateInodeBitMap(int inodeNum)
{
    superBlock.free_inode_count++;
    int whichGroup = (inodeNum - 1) / superBlock.inodes_per_group;
    int localPos = (inodeNum - 1) % superBlock.inodes_per_group;
    descs[whichGroup]->free_inode_count++;
    int bitmapInode = descs[whichGroup]->inode_bitmap;
    unsigned char inodeBMap[blockSize];
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapInode), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapInode), SEEK_SET);
    }
    read(imageFD, inodeBMap, blockSize);
    unsigned char bytePos = inodeBMap[(inodeNum - 1) / 8];
    int bitPos = (inodeNum - 1) % 8;
    inodeBMap[(inodeNum - 1) / 8] = bytePos & ~(1 << bitPos);
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapInode), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapInode), SEEK_SET);
    }
    write(imageFD, inodeBMap, blockSize);
    writeSuperBlock(&superBlock, imageFD);
    writeGroupDescriptor(imageFD, descs[whichGroup], whichGroup);
}

void allocateInodeBitMap(int inodeNum)
{
    //cout << "Inode Number:" << inodeNum << endl;
    superBlock.free_inode_count--;
    int whichGroup = (inodeNum - 1) / superBlock.inodes_per_group;
    int localPos = (inodeNum - 1) % superBlock.inodes_per_group;
    //cout << "Which group:" << whichGroup << endl;
    descs[whichGroup]->free_inode_count--;
    int bitmapInode = descs[whichGroup]->inode_bitmap;
    unsigned char inodeBMap[blockSize];
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapInode), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapInode), SEEK_SET);
    }
    read(imageFD, inodeBMap, blockSize);
    //cout << "Ilk read" << endl;
    unsigned char bytePos = inodeBMap[(inodeNum - 1) / 8];
    int bitPos = (inodeNum - 1) % 8;
    inodeBMap[(inodeNum - 1) / 8] = bytePos | (1 << bitPos);
    if (blockSize == 1024)
    {
        lseek(imageFD, OFFSET(bitmapInode), SEEK_SET);
    }
    else
    {
        lseek(imageFD, OFFSET2(bitmapInode), SEEK_SET);
    }
    write(imageFD, inodeBMap, blockSize);
    writeSuperBlock(&superBlock, imageFD);
    writeGroupDescriptor(imageFD, descs[whichGroup], whichGroup);
}
/**
 * If new block was allocated return that block number otherwise -1.
*/
int addDirectoryEntry(ext2_dir_entry *dirEntry, ext2_inode *dirInode, int dirInodeNum, ext2_inode *allocatedInode, int allocatedInodeNum)
{
    int allocated = -1;
    unsigned int mySize = sizeof(dirEntry) + dirEntry->name_length;
    mySize = round4(mySize);
    bool allocationFlag = false;
    for (int i = 0; i < 12; i++)
    {
        if (dirInode->direct_blocks[i] == 0)
        {
            // Yeni block allocate etme işi burda yapılıyor.
            allocated = getFirstFreeBlock();
            //cout << "Allocated: " << allocated << endl;
            if (blockSize == 1024)
                incrementBlockRefMap(allocated - 1);
            else
                incrementBlockRefMap(allocated);
            //cout << "?" << endl;
            allocateBlockBitMap(allocated);
            //cout << "?" << endl;
            dirInode->direct_blocks[i] = allocated;
            int blockIncrement = blockSize / 512;
            dirInode->block_count_512 += blockIncrement;
            dirInode->size += blockSize;
            writeInode(imageFD, dirInodeNum, dirInode);
            //cout << "?" << endl;
            allocationFlag = true;
            //cout << "?" << endl;
        }
        char blockData[blockSize];
        if (blockSize == 1024)
        {
            lseek(imageFD, OFFSET(dirInode->direct_blocks[i]), SEEK_SET);
        }
        else
        {
            lseek(imageFD, OFFSET2(dirInode->direct_blocks[i]), SEEK_SET);
        }
        read(imageFD, blockData, blockSize);
        ext2_dir_entry *thisDir = (ext2_dir_entry *)blockData;
        int currentPos = 0;
        while (currentPos < blockSize)
        {

            if (thisDir->inode == 0 && mySize <= thisDir->length)
            {
                // Burda ekleme yapılacak
                thisDir->inode = dirEntry->inode;
                thisDir->name_length = dirEntry->name_length;
                thisDir->file_type = dirEntry->file_type;
                memcpy(thisDir->name, dirEntry->name, dirEntry->name_length);
                if (blockSize == 1024)
                {
                    lseek(imageFD, OFFSET(dirInode->direct_blocks[i]), SEEK_SET);
                }
                else
                {
                    lseek(imageFD, OFFSET2(dirInode->direct_blocks[i]), SEEK_SET);
                }
                write(imageFD, blockData, blockSize);
                return allocated;
            }
            unsigned int thisSize = sizeof(thisDir) + thisDir->name_length;
            thisSize = round4(thisSize);
            unsigned int bosluk = thisDir->length - thisSize;

            // Arada yeterli boşluk varsa oraya eklenecek
            // Bi önceki record lengthi ona göre güncellenecek
            //currentPos += thisSize;
            if (bosluk >= mySize && (currentPos + mySize <= blockSize))
            {
                unsigned int onceki = thisDir->length;

                unsigned int fark = thisDir->length - thisSize;
                int prevLength = sizeof(thisDir) + thisDir->name_length;
                prevLength = round4(prevLength);
                thisDir->length = prevLength;
                dirEntry->length = fark;

                thisDir = (ext2_dir_entry *)((char *)thisDir + thisSize);
                thisDir->inode = dirEntry->inode;
                thisDir->file_type = dirEntry->file_type;
                thisDir->length = dirEntry->length;
                thisDir->name_length = dirEntry->name_length;

                memcpy(thisDir->name, dirEntry->name, dirEntry->name_length);
                if (blockSize == 1024)
                {
                    lseek(imageFD, OFFSET(dirInode->direct_blocks[i]), SEEK_SET);
                }
                else
                {
                    lseek(imageFD, OFFSET2(dirInode->direct_blocks[i]), SEEK_SET);
                }
                write(imageFD, blockData, blockSize);
                return allocated;
            }
            // Boşluk bulunamadığında
            // Bir sonraki entrye geç.
            currentPos += thisDir->length;
            thisDir = (ext2_dir_entry *)((char *)thisDir + thisDir->length);
        }
    }
    return allocated;
}

int removeDirectoryEntry(char *filename, ext2_inode *searched)
{
    unsigned char blockData[blockSize];
    int removed = 0;
    for (int i = 0; i < 12; i++)
    {
        if (blockSize == 1024)
        {
            lseek(imageFD, OFFSET(searched->direct_blocks[i]), SEEK_SET);
        }
        else
        {
            lseek(imageFD, OFFSET2(searched->direct_blocks[i]), SEEK_SET);
        }
        read(imageFD, blockData, blockSize);
        int currentPosition = 0;
        ext2_dir_entry *thisDir = (ext2_dir_entry *)blockData;
        //cout << "Length: " << thisDir->length << endl;
        //cout << "NameLength: " << unsigned(thisDir->name_length) << endl;
        //cout << "InodeNum: " << thisDir->inode << endl;
        int prevLength = 0;
        while (currentPosition < searched->size)
        {
            // We are searching through the block data
            // If it will exceed we will search the next direct block of inode.
            char entryFilename[EXT2_MAX_NAME_LENGTH + 1];
            memcpy(entryFilename, thisDir->name, unsigned(thisDir->name_length));
            //cout << "Enrty Filename: " << entryFilename << endl;
            entryFilename[unsigned(thisDir->name_length)] = '\0';
            //cout << "Gezilen: " << entryFilename << endl;
            // Compare the filenames if they are equal we are done!
            if (strcmp(entryFilename, filename) == 0)
            {
                if (currentPosition == 0)
                {
                    removed = thisDir->inode;
                    thisDir->inode = 0;
                    if (blockSize == 1024)
                    {
                        lseek(imageFD, OFFSET(searched->direct_blocks[i]), SEEK_SET);
                    }
                    else
                    {
                        lseek(imageFD, OFFSET2(searched->direct_blocks[i]), SEEK_SET);
                    }
                    write(imageFD, blockData, blockSize);
                    return removed;
                }
                else
                {
                    removed = thisDir->inode;
                    thisDir->inode = 0;
                    int addToPrev = thisDir->length;
                    thisDir = (ext2_dir_entry *)((char *)thisDir - prevLength);
                    thisDir->length = thisDir->length + addToPrev;
                    if (blockSize == 1024)
                    {
                        lseek(imageFD, OFFSET(searched->direct_blocks[i]), SEEK_SET);
                    }
                    else
                    {
                        lseek(imageFD, OFFSET2(searched->direct_blocks[i]), SEEK_SET);
                    }
                    write(imageFD, blockData, blockSize);
                    return removed;
                }
            }
            prevLength = thisDir->length;
            currentPosition += thisDir->length;
            thisDir = (ext2_dir_entry *)((char *)thisDir + thisDir->length);
        }
    }
    return -1;
}

void removeInode(int inodeNum, ext2_inode *inode)
{
    for (int i = 0; i < 12; i++)
    {
        if (inode->direct_blocks[i] != 0)
        {
            unsigned int decremented;
            if (blockSize == 1024){
                decremented = decrementBlockRefMap(inode->direct_blocks[i] - 1);
            }
            else{
                decremented = decrementBlockRefMap(inode->direct_blocks[i]);
            }

            //cout << "Decremented: " << decremented << endl;
            if (decremented == 0)
            {
                if (blockSize == 1024)
                {
                    deallocateBlockBitMap(inode->direct_blocks[i] - 1);
                }
                else
                {
                    deallocateBlockBitMap(inode->direct_blocks[i]);
                }
                removedBlocks.push_back(unsigned(inode->direct_blocks[i]));
            }
            inode->direct_blocks[i] = 0;
        }
    }
    unsigned int delTime = getTime();
    inode->deletion_time = delTime;
    inode->block_count_512 = 0;
    inode->size = 0;
    inode->link_count--;
    writeInode(imageFD, inodeNum, inode);
}

void showUsage()
{
    cerr << "You should give proper commands like:" << endl;
    cerr << "'dup' or 'rm' file system image with source and destination." << endl;
    cerr << "Example run:" << endl;
    cerr << "$ ./ext2utils dup FS_IMAGE SOURCE DEST" << endl;
}

int main(int argc, char *argv[])
{

    // argv[3] = source
    // argv[4] = destination
    if (argc < 2 || argc > 5)
    {
        showUsage();
        return 1;
    }
    if ((imageFD = open(argv[2], O_RDWR)) < 0)
    {
        perror("Image file could not opened!");
        exit(EXIT_FAILURE);
    }
    if (readSuperBlock(&superBlock, imageFD) != true)
    {
        exit(EXIT_SUCCESS);
    }
    //printSuperBlock(superBlock);
    //cout << "Block Size: " << blockSize << endl;

    /*
            After reading superblock we will store the all group descriptor informations
            And every group descriptor will be pushed to the vector.
    */
    for (int i = 0; i < groupCount; i++)
    {
        ext2_block_group_descriptor *temp = new ext2_block_group_descriptor;
        readGroupDescriptor(imageFD, temp, i);
        descs.push_back(temp);
        //printGroupDesc(temp);
    }
    if (strcmp(argv[1], "dup") == 0)
    {

        // Checking given source is inode number or directory.
        if (argv[3][0] == '/')
        {
            // Source is an absolute path which should be parsed first.
            // Also should start from the root directory which is inode number 2.
            // Save it on vector
            char delimiter[] = "/";
            char *elems = strtok(argv[3], delimiter);
            while (elems != NULL)
            {
                sourcePath.push_back(elems);
                elems = strtok(NULL, delimiter);
            }

            // Burda source inode'unu buluyorum.
            ext2_inode *startNode = new ext2_inode;
            int inodeNum = 2;
            readInode(imageFD, inodeNum, startNode);

            for (int l = 0; l < sourcePath.size(); l++)
            {
                //cout << "Searching for: " << sourcePath[l] << endl;
                inodeNum = getInodeOfGivenEntryName(sourcePath[l], startNode);
                //cout << "Source Path: " << sourcePath[l] << endl;
                //cout << "Length: " << strlen(sourcePath[l]) << endl;
                //cout << "Found inode_number: " << inodeNum << endl;
                if (inodeNum == -1)
                {
                    cout << "Invalid path!" << endl;
                    exit(EXIT_FAILURE);
                }
                readInode(imageFD, inodeNum, startNode);
            }
            // StartNode = source inode.
            // Eger destination' da path ise.
            if (argv[4][0] == '/')
            {

                // Parse edip vector'e atadim.
                // Bu sayede filename ve parent directorysine kolayca erisebilicem
                ext2_inode *destStartNode = new ext2_inode;
                readInode(imageFD, 2, destStartNode);
                char *destElems = strtok(argv[4], delimiter);
                while (destElems != NULL)
                {
                    destinationPath.push_back(destElems);
                    destElems = strtok(NULL, delimiter);
                }
                int inodeNum2;

                for (int b = 0; b < destinationPath.size() - 1; b++)
                {
                    inodeNum2 = getInodeOfGivenEntryName(destinationPath[b], destStartNode);
                    //cout << "Found inode_number: " << inodeNum2 << endl;
                    //cout << "Dest Path: " << destinationPath[b] << endl;
                    //cout << "Length: " << strlen(destinationPath[b]) << endl;
                    if (inodeNum2 == -1)
                    {
                        cout << "Invalid path!" << endl;
                        exit(EXIT_FAILURE);
                    }
                    readInode(imageFD, inodeNum2, destStartNode);
                }
                if (destinationPath.size() == 1)
                {
                    inodeNum2 = 2;
                }
                //cout << "Dest Path: " << destinationPath[destinationPath.size()-1] << endl;
                //cout << "Length: " << strlen(destinationPath[destinationPath.size()-1]) << endl;
                //cout << "Dest Path Bulma islemine girmemesi lazım" << endl;
                // destStartNode kaydedeceğim directory entryi tutan node.
                int freeNodeNum = getFirstFreeInode();
                //cout << "FreeInode: " << freeNodeNum << endl;
                // Olusturulan Inode'un bilgilerini kopyaladım.

                ext2_inode *newNode = copyInode(startNode);
                //cout << "StartNode: " << inodeNum << endl;
                //cout << "Allocate oncesi" << endl;
                // Inode Bitmap'de gerekli yer işaretlendi.
                allocateInodeBitMap(freeNodeNum);
                cout << freeNodeNum << endl;
                // Gerekli yere yeni oluşturulan inode yazıldı.

                // Gerekli blockların refcount'ları arttırıldı.
                //cout << "Yazma isleminden sonra" << endl;
                for (int c = 0; c < 12; c++)
                {
                    if (newNode->direct_blocks[c] == 0)
                    {
                        continue;
                    }
                    //cout << "Refmap: " << newNode->direct_blocks[c] << endl;
                    if (blockSize == 1024)
                        incrementBlockRefMap(newNode->direct_blocks[c] - 1);
                    else
                        incrementBlockRefMap(newNode->direct_blocks[c]);
                }
                //cout << "Block refmapler ayarlandi. " << endl;
                ext2_dir_entry *newEntry = new ext2_dir_entry;
                //cout << "For cikisi" << endl;
                newEntry->inode = (uint32_t)freeNodeNum;
                //cout << "Inode num: " << newEntry->inode << endl;
                //printDestinationPath();
                newEntry->name_length = static_cast<uint8_t>(strlen(destinationPath[destinationPath.size() - 1]));
                newEntry->file_type = 1;
                //cout << "NAME LENGTH: " << unsigned(newEntry->name_length) << endl;
                for (int a = 0; a < newEntry->name_length; a++)
                {
                    newEntry->name[a] = destinationPath[destinationPath.size() - 1][a];
                }
                //cout << "Add'a giden inode: " << inodeNum2 << endl;
                int results = addDirectoryEntry(newEntry, destStartNode, inodeNum2, newNode, freeNodeNum);
                //cout << freeNodeNum << endl;
                writeInode(imageFD, freeNodeNum, newNode);
                cout << results << endl;
            }
            else
            {
                // destination is an inode number.
                char *destElems = strtok(argv[4], delimiter);
                while (destElems != NULL)
                {
                    destinationPath.push_back(destElems);
                    destElems = strtok(NULL, delimiter);
                }
                int destinationNode = atoi(destinationPath[0]);
                ext2_inode *destStartNode = new ext2_inode;
                readInode(imageFD, destinationNode, destStartNode);
                int freeNodeNum = getFirstFreeInode();
                cout << freeNodeNum << endl;
                ext2_inode *newNode = new ext2_inode;
                newNode = copyInode(startNode);
                allocateInodeBitMap(freeNodeNum);
                writeInode(imageFD, freeNodeNum, newNode);
                for (int c = 0; c < 12; c++)
                {
                    if (newNode->direct_blocks[c] == 0)
                    {
                        //cout << "Breaked Dest?" << endl;
                        break;
                    }
                    //cout << "Refmap: " << newNode->direct_blocks[c] << endl;
                    if (blockSize == 1024)
                        incrementBlockRefMap(newNode->direct_blocks[c] - 1);
                    else
                        incrementBlockRefMap(newNode->direct_blocks[c]);
                }
                ext2_dir_entry *newEntry = new ext2_dir_entry;
                //cout << "For cikisi" << endl;
                newEntry->inode = (uint32_t)freeNodeNum;
                //cout << "Inode num: " << newEntry->inode << endl;
                //printDestinationPath();
                newEntry->name_length = static_cast<uint8_t>(strlen(destinationPath[destinationPath.size() - 1]));
                newEntry->file_type = 1;
                for (int a = 0; a < newEntry->name_length; a++)
                {
                    newEntry->name[a] = destinationPath[destinationPath.size() - 1][a];
                }
                int results = addDirectoryEntry(newEntry, destStartNode, destinationNode, newNode, freeNodeNum);
                //cout << freeNodeNum << endl;
                cout << results << endl;
            }
        }
        else
        {
            // Source is an inode number which is integer.
            int inodeNum = atoi(argv[3]);
            ext2_inode *startNode = new ext2_inode;
            readInode(imageFD, inodeNum, startNode);
            ext2_inode *copied = new ext2_inode;
            copied = copyInode(startNode);
            char delimiter[] = "/";
            if (argv[4][0] == '/')
            {

                // Parse edip vector'e atadim.
                // Bu sayede filename ve parent directorysine kolayca erisebilicem
                ext2_inode *destStartNode = new ext2_inode;
                readInode(imageFD, 2, destStartNode);
                char *destElems = strtok(argv[4], delimiter);
                while (destElems != NULL)
                {
                    destinationPath.push_back(destElems);
                    destElems = strtok(NULL, delimiter);
                }
                int inodeNum2;

                for (int b = 0; b < destinationPath.size() - 1; b++)
                {
                    inodeNum2 = getInodeOfGivenEntryName(destinationPath[b], destStartNode);
                    //cout << "Found inode_number: " << inodeNum << endl;
                    //cout << "Dest Path: " << destinationPath[b] << endl;
                    //cout << "Length: " << strlen(destinationPath[b]) << endl;
                    if (inodeNum2 == -1)
                    {
                        cout << "Invalid path!" << endl;
                        exit(EXIT_FAILURE);
                    }
                    readInode(imageFD, inodeNum2, destStartNode);
                }
                //cout << "Dest Path: " << destinationPath[destinationPath.size()-1] << endl;
                //cout << "Length: " << strlen(destinationPath[destinationPath.size()-1]) << endl;
                //cout << "Dest Path Bulma islemine girmemesi lazım" << endl;
                // destStartNode kaydedeceğim directory entryi tutan node.
                int freeNodeNum = getFirstFreeInode();
                //cout << "FreeInode: " << freeNodeNum << endl;
                // Olusturulan Inode'un bilgilerini kopyaladım.

                ext2_inode *newNode = new ext2_inode;
                newNode = copyInode(startNode);

                //cout << "Allocate oncesi" << endl;
                // Inode Bitmap'de gerekli yer işaretlendi.
                allocateInodeBitMap(freeNodeNum);
                //cout << "Burda?" << endl;
                // Gerekli yere yeni oluşturulan inode yazıldı.
                writeInode(imageFD, freeNodeNum, newNode);
                // Gerekli blockların refcount'ları arttırıldı.
                //cout << "Yazma isleminden sonra" << endl;
                for (int c = 0; c < 12; c++)
                {
                    if (newNode->direct_blocks[c] == 0)
                    {
                        break;
                    }
                    //cout << "Refmap: " << newNode->direct_blocks[c] << endl;
                    if (blockSize == 1024)
                        incrementBlockRefMap(newNode->direct_blocks[c] - 1);
                    else
                        incrementBlockRefMap(newNode->direct_blocks[c]);
                }
                //cout << "Block refmapler ayarlandi. " << endl;
                ext2_dir_entry *newEntry = new ext2_dir_entry;
                //cout << "For cikisi" << endl;
                newEntry->inode = (uint32_t)freeNodeNum;
                //cout << "Inode num: " << newEntry->inode << endl;
                //printDestinationPath();
                newEntry->name_length = static_cast<uint8_t>(strlen(destinationPath[destinationPath.size() - 1]));
                newEntry->file_type = 1;
                for (int a = 0; a < newEntry->name_length; a++)
                {
                    newEntry->name[a] = destinationPath[destinationPath.size() - 1][a];
                }
                int results = addDirectoryEntry(newEntry, destStartNode, inodeNum2, newNode, freeNodeNum);
                cout << freeNodeNum << endl;
                cout << results << endl;
            }
            else
            {
                // destination is an inode number.
                char *destElems = strtok(argv[4], delimiter);
                while (destElems != NULL)
                {
                    destinationPath.push_back(destElems);
                    destElems = strtok(NULL, delimiter);
                }
                int destinationNode = atoi(destinationPath[0]);
                ext2_inode *destStartNode = new ext2_inode;
                readInode(imageFD, destinationNode, destStartNode);
                int freeNodeNum = getFirstFreeInode();
                ext2_inode *newNode = new ext2_inode;
                newNode = copyInode(startNode);
                allocateInodeBitMap(freeNodeNum);
                writeInode(imageFD, freeNodeNum, newNode);
                for (int c = 0; c < 12; c++)
                {
                    if (newNode->direct_blocks[c] == 0)
                    {
                        break;
                    }
                    //cout << "Refmap: " << newNode->direct_blocks[c] << endl;
                    if (blockSize == 1024)
                        incrementBlockRefMap(newNode->direct_blocks[c] - 1);
                    else
                        incrementBlockRefMap(newNode->direct_blocks[c]);
                }
                ext2_dir_entry *newEntry = new ext2_dir_entry;
                //cout << "For cikisi" << endl;
                newEntry->inode = (uint32_t)freeNodeNum;
                //cout << "Inode num: " << newEntry->inode << endl;
                //printDestinationPath();
                newEntry->name_length = static_cast<uint8_t>(strlen(destinationPath[destinationPath.size() - 1]));
                newEntry->file_type = 1;
                for (int a = 0; a < newEntry->name_length; a++)
                {
                    newEntry->name[a] = destinationPath[destinationPath.size() - 1][a];
                }
                int results = addDirectoryEntry(newEntry, destStartNode, destinationNode, newNode, freeNodeNum);
                cout << freeNodeNum << endl;
                cout << results << endl;
            }
        }
        return 0;
    }
    if (strcmp(argv[1], "rm") == 0)
    {
        if (argv[3][0] == '/')
        {
            char delimiter[] = "/";
            char *elems = strtok(argv[3], delimiter);
            while (elems != NULL)
            {
                sourcePath.push_back(elems);
                elems = strtok(NULL, delimiter);
            }

            // Burda source inode'unu buluyorum.
            ext2_inode *startNode = new ext2_inode;
            int inodeNum = 2;
            readInode(imageFD, inodeNum, startNode);
            int parentInodeNum;

            for (int l = 0; l < sourcePath.size(); l++)
            {
                //cout << "Searching for: " << sourcePath[l] << endl;
                int inodeNum = getInodeOfGivenEntryName(sourcePath[l], startNode);
                //cout << "Source Path: " << sourcePath[l] << endl;
                //cout << "Length: " << strlen(sourcePath[l]) << endl;
                //cout << "Found inode_number: " << inodeNum << endl;
                if (inodeNum == -1)
                {
                    cout << "Invalid path!" << endl;
                    exit(EXIT_FAILURE);
                }
                if (l == (sourcePath.size() - 2))
                {
                    parentInodeNum = inodeNum;
                }
                readInode(imageFD, inodeNum, startNode);
            }

            if (sourcePath.size() == 1)
            {
                parentInodeNum = 2;
            }
            // startNode file'ı gösteren node.
            ext2_inode *parentNode = new ext2_inode;
            // parentNode belli zaten
            readInode(imageFD, parentInodeNum, parentNode);
            int removedNodeNum = removeDirectoryEntry(sourcePath[sourcePath.size() - 1], parentNode);
            if (inodeNum != removedNodeNum)
            {
                //cout << "Hatali bir eylem gerçeklesti!" << endl;
            }
            if (startNode->link_count > 1)
            {
                startNode->link_count -= 1;
                writeInode(imageFD, removedNodeNum, startNode);
                cout << removedNodeNum << endl;
                cout << -1 << endl;
            }
            else
            {
                deallocateInodeBitMap(removedNodeNum);
                removeInode(removedNodeNum, startNode);
                cout << removedNodeNum << endl;
                if (removedBlocks.size() == 0)
                {
                    cout << -1 << endl;
                }
                else
                {
                    for (int j = 0; j < removedBlocks.size(); j++)
                    {
                        if (j == (removedBlocks.size() - 1))
                        {
                            cout << removedBlocks[j] << endl;
                        }
                        else
                        {
                            cout << removedBlocks[j] << " ";
                        }
                    }
                }
            }
            return 0;
        }
        else
        {
            char delimiter[] = "/";
            char *elems = strtok(argv[3], delimiter);
            int parentInodeNum;
            while (elems != NULL)
            {
                sourcePath.push_back(elems);
                elems = strtok(NULL, delimiter);
            }
            parentInodeNum = atoi(sourcePath[0]);
            ext2_inode *fileNode = new ext2_inode;
            ext2_inode *parentInode = new ext2_inode;
            readInode(imageFD, parentInodeNum, parentInode);
            int fileNum = getInodeOfGivenEntryName(sourcePath[1], parentInode);
            //cout << "sp[1]" << sourcePath[1] << endl;
            readInode(imageFD, fileNum, fileNode);
            int removedNum = removeDirectoryEntry(sourcePath[1], parentInode);
            if (removedNum != fileNum)
            {
                cerr << "Something wrong happened!" << endl;
            }
            if (fileNode->link_count > 1)
            {
                fileNode->link_count -= 1;
                writeInode(imageFD, fileNum, fileNode);
                cout << fileNum << endl;
                cout << -1 << endl;
            }
            else
            {
                deallocateInodeBitMap(fileNum);
                removeInode(fileNum, fileNode);
                cout << fileNum << endl;
                if (removedBlocks.size() == 0)
                {
                    cout << -1 << endl;
                }
                else
                {
                    for (int j = 0; j < removedBlocks.size(); j++)
                    {
                        if (j == (removedBlocks.size() - 1))
                        {
                            cout << removedBlocks[j] << endl;
                        }
                        else
                        {
                            cout << removedBlocks[j] << " ";
                        }
                    }
                }
            }
        }
        return 0;
    }
    else
    {
        cerr << "Wrong input!" << endl;
        exit(EXIT_FAILURE);
    }
}
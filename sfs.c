#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define superBlockIndex 0
#define blockBitMapIndex 1
#define inodeBitMapIndex 2
#define inodeTableIndex 3
#define maxBlocks 99
#define maxInodes 127

// structure of an inode entry
typedef struct
{
    char TT[2]; // entry type; "DI" = directory, "FI" = file
    char XX[2];
    char YY[2];
    char ZZ[2];
} _inode_entry;

// structure of a directory entry
typedef struct
{
    char F;          // '1' = used | '0' = unused
    char fname[252]; // File/Directory Name
    char MMM[3];     // Inode table index
} _directory_entry;

// SFS metadata; read during mounting
int BLB;                        // total number of blocks
int INB;                        // total number of entries in inode table
char _block_bitmap[1024];       // the block bitmap array
char _inode_bitmap[1024];       // the inode bitmap array
_inode_entry _inode_table[128]; // the inode table containing 128 inode entries

// useful info
int freeDiskBlocks;                       // number of available disk blocks
int freeInodeEntries;                     // number of available entries in inode table
int currentDirectoryInode = 0;            // index of inode entry of the current directory in the inode table
char currrentWorkingDirectory[252] = "/"; // name of current directory (useful in the prompt)

FILE *diskFile = NULL; // THE DISK FILE (File Descriptor)

// function declarations

// DISK ACCESS
void mountMetaData();
int readBlock(int, char *);
int writeBlock(int, char *);

// BITMAP ACCESS
int getBlock();
void returnBlock(int);
int getInode();
void returnInode(int);

// COMMANDS
void ls();
void rd();
void cd(char *);
void md(char *);
void stats();
void create(char *);
void rm(char *);
void display(char *);

// HELPERS
int stoi(char *, int);
void itos(char *, int, int);
void printPrompt();

// Convert string to integer
int stoi(char *s, int n)
{
    int i;
    int ret = 0;

    for (i = 0; i < n; i++)
    {
        if (s[i] < 48 || s[i] > 57)
            return -1; // non-digit
        ret += pow(10, n - i - 1) * (s[i] - 48);
    }

    return ret;
}

// Convert Integer to String
void itos(char *s, int num, int n)
{
    char st[1024];
    sprintf(st, "%0*d", n, num);
    strncpy(s, st, n);
}

// Print Prompt like Shell
void printPrompt()
{
    printf("SFS::%s# ", currrentWorkingDirectory);
}

// Open file and read metadata + bitmaps + inode table
void mountMetaData()
{
    int i;
    char buffer[1024];

    diskFile = fopen("sfs.disk", "r+b");
    if (diskFile == NULL)
    {
        printf("Disk file sfs.disk not found.\n");
        exit(1);
    }

    // read superblock
    fread(buffer, 1, 1024, diskFile);
    BLB = stoi(buffer, 3);
    INB = stoi(buffer + 3, 3);

    // read block bitmap
    fread(_block_bitmap, 1, 1024, diskFile);
    // initialize number of free disk blocks
    freeDiskBlocks = BLB;
    for (i = 0; i < BLB; i++)
        freeDiskBlocks -= (_block_bitmap[i] - 48);

    // read inode bitmap
    fread(_inode_bitmap, 1, 1024, diskFile);
    // initialize number of unused inode entries
    freeInodeEntries = INB;
    for (i = 0; i < INB; i++)
        freeInodeEntries -= (_inode_bitmap[i] - 48);

    // read the inode table
    fread(_inode_table, 1, 1024, diskFile);
}

// Read block data
int readBlock(int block_number, char buffer[1024])
{
    if (block_number < 0 || block_number > maxBlocks)
    {
        return 0;
    }

    if (diskFile == NULL)
    {
        mountMetaData();
    }

    fseek(diskFile, block_number * 1024, SEEK_SET);
    fread(buffer, 1, 1024, diskFile);

    return 1;
}

// Write data in disk file
int writeBlock(int block_number, char buffer[1024])
{
    char empty_buffer[1024];

    if (block_number < 0 || block_number > maxBlocks)
    {
        return 0;
    }

    if (diskFile == NULL)
    {
        mountMetaData();
    }

    fseek(diskFile, block_number * 1024, SEEK_SET); // set file pointer at right position

    if (buffer == NULL)
    {
        memset(empty_buffer, '0', 1024);
        fwrite(empty_buffer, 1, 1024, diskFile);
    }
    else
    {
        fwrite(buffer, 1, 1024, diskFile);
    }

    fflush(diskFile);

    return 1;
}

// Return First Available block index
int getBlock()
{
    if (freeDiskBlocks == 0)
    {
        return -1;
    }

    int i;
    for (i = 0; i < BLB; i++)
    {
        if (_block_bitmap[i] == '0')
        {
            break; // 0 means available
        }
    }

    _block_bitmap[i] = '1';
    freeDiskBlocks--;

    writeBlock(blockBitMapIndex, _block_bitmap);

    return i;
}

// Free unused block
void returnBlock(int index)
{
    if (index > 3 && index <= maxBlocks)
    {
        _block_bitmap[index] = '0';
        freeDiskBlocks++;

        writeBlock(blockBitMapIndex, _block_bitmap);
    }
}

// Return first available inode
int getInode()
{
    if (freeInodeEntries == 0)
    {
        return -1;
    }

    int i;
    for (i = 0; i < INB; i++)
    {
        if (_inode_bitmap[i] == '0')
        {
            break; // 0 means available
        }
    }

    _inode_bitmap[i] = '1';
    freeInodeEntries--;

    writeBlock(inodeBitMapIndex, _inode_bitmap);

    return i;
}

// Free unused Inode
void returnInode(int index)
{
    if (index > 0 && index <= maxInodes)
    {
        _inode_bitmap[index] = '0';
        freeInodeEntries++;

        writeBlock(inodeBitMapIndex, _inode_bitmap);
    }
}

// Make root directory current working directory
void rd()
{
    currentDirectoryInode = 0; // first inode entry is for root directory
    currrentWorkingDirectory[0] = '/';
    currrentWorkingDirectory[1] = 0;
}

// List all file ans directories in current working directory
void ls()
{
    char inodeType;
    int blocks[3];
    _directory_entry _directory_entries[4];

    int total_files = 0, total_dirs = 0;

    int i, j;
    int e_inode;

    // read inode entry for current directory
    // in SFS, an inode can point to three blocks at the most
    inodeType = _inode_table[currentDirectoryInode].TT[0];
    blocks[0] = stoi(_inode_table[currentDirectoryInode].XX, 2);
    blocks[1] = stoi(_inode_table[currentDirectoryInode].YY, 2);
    blocks[2] = stoi(_inode_table[currentDirectoryInode].ZZ, 2);

    // its a directory; so the following should never happen
    if (inodeType == 'F')
    {
        printf("Fatal Error! Aborting.\n");
        exit(1);
    }

    // lets traverse the directory entries in all three blocks
    for (i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
            continue; // 0 means pointing at nothing

        readBlock(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

        // so, we got four possible directory entries now
        for (j = 0; j < 4; j++)
        {
            if (_directory_entries[j].F == '0')
                continue; // means unused entry

            e_inode = stoi(_directory_entries[j].MMM, 3); // this is the inode that has more info about this entry

            if (_inode_table[e_inode].TT[0] == 'F')
            { // entry is for a file
                printf("%.252s\t", _directory_entries[j].fname);
                total_files++;
            }
            else if (_inode_table[e_inode].TT[0] == 'D')
            { // entry is for a directory; print it in BRED
                printf("\e[1;31m%.252s\e[;;m\t", _directory_entries[j].fname);
                total_dirs++;
            }
        }
    }

    printf("\n%d file%c and %d director%s.\n", total_files, (total_files <= 1 ? 0 : 's'), total_dirs, (total_dirs <= 1 ? "y" : "ies"));
}

// Move to directory
void cd(char *dname)
{
    char inodeType;
    int blocks[3];
    _directory_entry _directory_entries[4];

    int i, j;
    int e_inode;

    char found = 0;

    // read inode entry for current directory
    // in SFS, an inode can point to three blocks at the most
    inodeType = _inode_table[currentDirectoryInode].TT[0];
    blocks[0] = stoi(_inode_table[currentDirectoryInode].XX, 2);
    blocks[1] = stoi(_inode_table[currentDirectoryInode].YY, 2);
    blocks[2] = stoi(_inode_table[currentDirectoryInode].ZZ, 2);

    // its a directory; so the following should never happen
    if (inodeType == 'F')
    {
        printf("Fatal Error! Aborting.\n");
        exit(1);
    }

    // now lets try to see if a directory by the name already exists
    for (i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
            continue; // 0 means pointing at nothing

        readBlock(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

        // so, we got four possible directory entries now
        for (j = 0; j < 4; j++)
        {
            if (_directory_entries[j].F == '0')
                continue; // means unused entry

            e_inode = stoi(_directory_entries[j].MMM, 3); // this is the inode that has more info about this entry

            if (_inode_table[e_inode].TT[0] == 'D')
            { // entry is for a directory; can't cd into a file, right?
                if (strncmp(dname, _directory_entries[j].fname, 252) == 0)
                {              // and it is the one we are looking for
                    found = 1; // VOILA
                    break;
                }
            }
        }
        if (found)
            break; // no need to search more
    }

    if (found)
    {
        currentDirectoryInode = e_inode;               // just keep track of which inode entry in the table corresponds to this directory
        strncpy(currrentWorkingDirectory, dname, 252); // can use it in the prompt
    }
    else
    {
        printf("%.252s: No such directory.\n", dname);
    }
}

// Create new directory
void md(char *dname)
{
    char inodeType;
    int blocks[3];
    _directory_entry _directory_entries[4];

    int i, j;

    int empty_dblock = -1, empty_dentry = -1;
    int empty_ientry;

    // non-empty name
    if (strlen(dname) == 0)
    {
        printf("Usage: md <directory name>\n");
        return;
    }

    // do we have free inodes
    if (freeInodeEntries == 0)
    {
        printf("Error: Inode table is full.\n");
        return;
    }

    // read inode entry for current directory
    // in SFS, an inode can point to three blocks at the most
    inodeType = _inode_table[currentDirectoryInode].TT[0];
    blocks[0] = stoi(_inode_table[currentDirectoryInode].XX, 2);
    blocks[1] = stoi(_inode_table[currentDirectoryInode].YY, 2);
    blocks[2] = stoi(_inode_table[currentDirectoryInode].ZZ, 2);

    // its a directory; so the following should never happen
    if (inodeType == 'F')
    {
        printf("Fatal Error! Aborting.\n");
        exit(1);
    }

    // now lets try to see if the name already exists
    for (i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
        { // 0 means pointing at nothing
            if (empty_dblock == -1)
                empty_dblock = i;
            continue;
        }

        readBlock(blocks[i], (char *)_directory_entries); // lets read a directory entry; notice the cast

        // so, we got four possible directory entries now
        for (j = 0; j < 4; j++)
        {
            if (_directory_entries[j].F == '0')
            { // means unused entry
                if (empty_dentry == -1)
                {
                    empty_dentry = j;
                    empty_dblock = i;
                }
                continue;
            }

            if (strncmp(dname, _directory_entries[j].fname, 252) == 0)
            { // compare with user given name
                printf("%.252s: Already exists.\n", dname);
                return;
            }
        }
    }
    // so directory name is new

    // if we did not find an empty directory entry and all three blocks are in use; then no new directory can be made
    if (empty_dentry == -1 && empty_dblock == -1)
    {
        printf("Error: Maximum directory entries reached.\n");
        return;
    }
    else
    {
        if (empty_dentry == -1)
        {
            empty_dentry = 0;

            if ((blocks[empty_dblock] = getBlock()) == -1)
            { // first get a new block using the block bitmap
                printf("Error: Disk is full.\n");
                return;
            }

            writeBlock(blocks[empty_dblock], NULL);

            switch (empty_dblock)
            {
            case 0:
                itos(_inode_table[currentDirectoryInode].XX, blocks[empty_dblock], 2);
                break;
            case 1:
                itos(_inode_table[currentDirectoryInode].YY, blocks[empty_dblock], 2);
                break;
            case 2:
                itos(_inode_table[currentDirectoryInode].ZZ, blocks[empty_dblock], 2);
                break;
            }
        }

        empty_ientry = getInode();

        readBlock(blocks[empty_dblock], (char *)_directory_entries);
        _directory_entries[empty_dentry].F = '1';
        strncpy(_directory_entries[empty_dentry].fname, dname, 252);
        itos(_directory_entries[empty_dentry].MMM, empty_ientry, 3);
        writeBlock(blocks[empty_dblock], (char *)_directory_entries);

        strncpy(_inode_table[empty_ientry].TT, "DI", 2);
        strncpy(_inode_table[empty_ientry].XX, "00", 2);
        strncpy(_inode_table[empty_ientry].YY, "00", 2);
        strncpy(_inode_table[empty_ientry].ZZ, "00", 2);

        writeBlock(inodeTableIndex, (char *)_inode_table);
    }
}

void stats()
{
    int blocks_free = BLB, inodes_free = INB;
    int i;

    for (i = 0; i < BLB; i++)
        blocks_free -= (_block_bitmap[i] - 48);
    for (i = 0; i < INB; i++)
        inodes_free -= (_inode_bitmap[i] - 48);

    printf("%d block%c free.\n", blocks_free, (blocks_free <= 1 ? 0 : 's'));
    printf("%d inode entr%s free.\n", inodes_free, (inodes_free <= 1 ? "y" : "ies"));
}

void display(char *fname)
{
    char inodeType;
    int blocks[3];
    _directory_entry _directory_entries[4];

    int i, j;
    int e_inode;

    char found = 0;
    char read_buffer[1024];

    inodeType = _inode_table[currentDirectoryInode].TT[0];
    blocks[0] = stoi(_inode_table[currentDirectoryInode].XX, 2);
    blocks[1] = stoi(_inode_table[currentDirectoryInode].YY, 2);
    blocks[2] = stoi(_inode_table[currentDirectoryInode].ZZ, 2);

    // This should never happen
    if (inodeType == 'F')
    {
        printf("Fatal Error! Aborting.\n");
        exit(1);
    }

    for (i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
            continue;

        readBlock(blocks[i], (char *)_directory_entries);

        for (j = 0; j < 4; j++)
        {
            if (_directory_entries[j].F == '0')
                continue;

            e_inode = stoi(_directory_entries[j].MMM, 3);

            if (_inode_table[e_inode].TT[0] == 'F')
            {
                if (strncmp(fname, _directory_entries[j].fname, 252) == 0)
                {
                    found = 1;
                    break;
                }
            }
        }
        if (found)
        {
            break;
        }
    }

    if (found)
    {
        int blocks[3];
        blocks[0] = stoi(_inode_table[e_inode].XX, 2);
        blocks[1] = stoi(_inode_table[e_inode].YY, 2);
        blocks[2] = stoi(_inode_table[e_inode].ZZ, 2);

        for (int i = 0; i < 3; i++)
        {
            if (blocks[i] != 0)
            {
                readBlock(blocks[i], read_buffer);
                printf("%s", read_buffer);
            }
        }
        printf("\n");
    }
    else
    {
        printf("%.252s: No such file.\n", fname);
    }
}

void create(char *fname)
{
    char inodeType;
    int blocks[3];
    int emptyBlockIndex = -1, emptyDirectoryEntryIndex = -1;
    int createNewBlockFlag = 0;
    int newInode;

    inodeType = _inode_table[currentDirectoryInode].TT[0];

    // This should never happen
    if (inodeType == 'F')
    {
        printf("Create Error: currentDirectoryInode is a file.\n");
        exit(1);
    }

    // Read directory entry blocks
    blocks[0] = stoi(_inode_table[currentDirectoryInode].XX, 2);
    blocks[1] = stoi(_inode_table[currentDirectoryInode].YY, 2);
    blocks[2] = stoi(_inode_table[currentDirectoryInode].ZZ, 2);

    // Check if file already exists in current directory
    // And find empty block and directory entry
    for (int i = 0; i < 3; i++)
    {
        // If block if empty then set emptyBlockIndex and emptyDirectoryEntryIndex and createNewBlockFlag
        // Priority: XX > YY > ZZ
        if (blocks[i] == 0)
        {
            emptyBlockIndex = i;
            emptyDirectoryEntryIndex = 0;
            createNewBlockFlag = 1;
            continue;
        }

        // Read directory entries
        _directory_entry directories[4];
        readBlock(blocks[i], (char *)directories);

        // Check if file already exists If not then set emptyBlockIndex and emptyDirectoryEntryIndex and createNewBlockFlag
        for (int j = 0; j < 4; j++)
        {
            if (directories[j].F == '1')
            {
                if (strcmp(directories[j].fname, fname) == 0)
                {
                    printf("%s: Already exists.\n", fname);
                    return;
                }
            }
            else
            {
                emptyBlockIndex = i;
                emptyDirectoryEntryIndex = j;
                createNewBlockFlag = 0;
            }
        }
    }

    // If emptyBlockIndex or emptyDirectoryEntryIndex are not set then file system is full
    if (emptyBlockIndex == -1 || emptyDirectoryEntryIndex == -1)
    {
        printf("File system is full: There is no empty space in this directory!\n");
        return;
    }
    // If createNewBlockFlag is set then create new block and new inode
    else if (createNewBlockFlag == 1)
    {
        int newBlock = getBlock();
        if (newBlock == -1)
        {
            printf("File system is full: No data blocks available!\n");
            return;
        }

        writeBlock(newBlock, NULL);
        newInode = getInode();

        if (newInode == -1)
        {
            returnBlock(newBlock);
            printf("File system is full: No inodes available!\n");
            return;
        }

        if (emptyBlockIndex == 0)
            itos(_inode_table[currentDirectoryInode].XX, newBlock, 2);
        else if (emptyBlockIndex == 1)
            itos(_inode_table[currentDirectoryInode].YY, newBlock, 2);
        else if (emptyBlockIndex == 2)
            itos(_inode_table[currentDirectoryInode].ZZ, newBlock, 2);
        blocks[emptyBlockIndex] = newBlock;
    }
    // If createNewBlockFlag is not set then create new inode
    else
    {
        newInode = getInode();
        if (newInode == -1)
        {
            printf("File system is full: No inodes available!\n");
            return;
        }
    }

    // Create new directory entry
    _directory_entry newDirectoryEntry[4];
    readBlock(blocks[emptyBlockIndex], (char *)newDirectoryEntry);

    // Set new directory entry data
    newDirectoryEntry[emptyDirectoryEntryIndex].F = '1';
    strncpy(newDirectoryEntry[emptyDirectoryEntryIndex].fname, fname, 252);
    itos(newDirectoryEntry[emptyDirectoryEntryIndex].MMM, newInode, 3);

    // Write new directory entry
    writeBlock(blocks[emptyBlockIndex], (char *)newDirectoryEntry);

    // Set inode table data
    strncpy(_inode_table[newInode].TT, "FI", 2);
    strncpy(_inode_table[newInode].XX, "00", 2);
    strncpy(_inode_table[newInode].YY, "00", 2);
    strncpy(_inode_table[newInode].ZZ, "00", 2);

    // Write inode table in disk
    writeBlock(inodeTableIndex, (char *)_inode_table);

    // Creation successfull :)
    printf("%s has been created, enter the text.\n", fname);

    // Read data from user and write it to file
    // While loop for reading maximum three blocks data brom the user
    int i = 0;
    while (i < 3)
    {
        // Create new inode for new data block
        int newBlock = getBlock();
        if (newBlock == -1)
        {
            printf("File system full: No data blocks!\n");
            printf("Data will be truncated!\n");
            return;
        }

        // Write index of new inode in inode table
        if (i == 0)
            itos(_inode_table[newInode].XX, newBlock, 2);
        else if (i == 1)
            itos(_inode_table[newInode].YY, newBlock, 2);
        else if (i == 2)
            itos(_inode_table[newInode].ZZ, newBlock, 2);

        // Read data until user press ESC(27)
        int j = 0;
        char read_buffer[1024];
        while (j < 1024)
        {
            scanf("%c", &read_buffer[j]);
            if (read_buffer[j] == 27)
            {
                read_buffer[j] = '\0';
                writeBlock(newBlock, read_buffer);
                fflush(stdin);
                return;
            }
            j++;
        }

        // Write full block data in disk
        writeBlock(newBlock, read_buffer);

        // Read abort because of maximum file size
        i++;
        if (i == 3)
        {
            printf("Maximum file size reached!\n");
            printf("Data will be truncated!\n");
            fflush(stdin);
            return;
        }
    }
}

// Helper function to delete file
/**
 * Read inode data from inode table
 * Read data blocks index from inode
 * Return data blocks to free block list
 * Return inode to free inode list
 * Write inode table in disk
 */
int removeFile(int inode)
{
    char inodeType = _inode_table[inode].TT[0];
    if (inodeType == 'D')
    {
        printf("Remove file error: inode is a directory!\n");
        exit(1);
    }
    int blocks[3];

    blocks[0] = stoi(_inode_table[inode].XX, 2);
    blocks[1] = stoi(_inode_table[inode].YY, 2);
    blocks[2] = stoi(_inode_table[inode].ZZ, 2);

    for (int i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
            continue;
        returnBlock(blocks[i]);
    }

    returnInode(inode);
    writeBlock(inodeTableIndex, (char *)_inode_table);
    return 1;
}

// Recursive helper function to delete directory
/**
 * Read inode data from inode table
 * Read data blocks index from inode
 * Traverse through all directory entries
 * If directory entry is a file then call removeFile()
 * If directory entry is a directory then call removeDirectory()
 * Return data blocks to free block list
 * Return inode to free inode list
 * Write inode table in disk
 */
int removeDirectory(int inode)
{
    char inodeType = _inode_table[inode].TT[0];
    if (inodeType == 'F')
    {
        printf("Remove directory error: inode is a directory!\n");
        exit(1);
    }

    int blocks[3];

    blocks[0] = stoi(_inode_table[inode].XX, 2);
    blocks[1] = stoi(_inode_table[inode].YY, 2);
    blocks[2] = stoi(_inode_table[inode].ZZ, 2);

    for (int i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
            continue;

        _directory_entry directories[4];
        readBlock(blocks[i], (char *)directories);

        for (int j = 0; j < 4; j++)
        {
            if (directories[j].F == '0')
                continue;

            int del_inode = stoi(directories[j].MMM, 3);
            inodeType = _inode_table[del_inode].TT[0];
            if (inodeType == 'F')
            {
                if (!removeFile(del_inode))
                {
                    printf("Remove File error: removeFile call failed!\n");
                    exit(1);
                }
            }
            else
            {
                if (!removeDirectory(del_inode))
                {
                    printf("Remove directory error: recursive removeDirectory call failed!\n");
                    exit(1);
                }
            }
            directories[j].F = '0';
        }
        writeBlock(blocks[i], (char *)directories);
        returnBlock(blocks[i]);
    }

    returnInode(inode);
    writeBlock(inodeTableIndex, (char *)_inode_table);
    return 1;
}

// Remove file or directory
/**
 * Read inode data from inode table
 * Read data blocks index from inode
 * Traverse through all directory entries
 * If directory entry is a file then call removeFile()
 * If directory entry is a directory then call removeDirectory()
 * Return data blocks to free block list
 * Return inode to free inode list
 * Write inode table in disk
 */
void rm(char *fdname)
{
    char inodeType = _inode_table[currentDirectoryInode].TT[0];
    if (inodeType == 'F')
    {
        printf("Remove error: CD_INODE is a file!\n");
        exit(1);
    }

    int blocks[3];
    int flag = 0;

    blocks[0] = stoi(_inode_table[currentDirectoryInode].XX, 2);
    blocks[1] = stoi(_inode_table[currentDirectoryInode].YY, 2);
    blocks[2] = stoi(_inode_table[currentDirectoryInode].ZZ, 2);

    for (int i = 0; i < 3; i++)
    {
        if (blocks[i] == 0)
            continue;

        _directory_entry directories[4];
        readBlock(blocks[i], (char *)directories);

        int cnt = 0;

        for (int j = 0; j < 4; j++)
        {
            if (directories[j].F == '0')
                continue;
            cnt++;
            if (strcmp(fdname, directories[j].fname) == 0)
            {
                flag = 1;
                int del_inode = stoi(directories[j].MMM, 3);
                inodeType = _inode_table[del_inode].TT[0];
                if (inodeType == 'F')
                    removeFile(del_inode);
                else
                    removeDirectory(del_inode);
                directories[j].F = '0';
                cnt--;
            }
        }
        writeBlock(blocks[i], (char *)directories);
        if (cnt == 0)
        {
            returnBlock(blocks[i]);

            if (i == 0)
                itos(_inode_table[currentDirectoryInode].XX, 0, 2);
            else if (i == 1)
                itos(_inode_table[currentDirectoryInode].YY, 0, 2);
            else if (i == 2)
                itos(_inode_table[currentDirectoryInode].ZZ, 0, 2);

            writeBlock(inodeTableIndex, (char *)_inode_table);
        }
    }
    if (flag == 0)
        printf("%s not found in current directory!\n", fdname);
}

int main()
{
    char cmdline[1024];
    int num_tokens = 0;
    char tokens[2][256];
    int i = 0;
    char *p;

    mountMetaData();

    while (1)
    {
        num_tokens = 0;
        i = 0;
        printPrompt();

        if (fgets(cmdline, 1024, stdin) == NULL)
        {
            printf("\n");
            break;
        }

        *strchr(cmdline, '\n') = '\0';

        p = cmdline;
        while (1 == sscanf(p, "%s", tokens[i]))
        {
            p = strstr(p, tokens[i]) + strlen(tokens[i]);
            i++;
        }
        num_tokens = i;

        if (num_tokens == 0)
            continue;

        if (num_tokens == 1)
        {
            if (strcmp(tokens[0], "ls") == 0)
                ls();
            else if (strcmp(tokens[0], "exit") == 0)
                break;
            else if (strcmp(tokens[0], "stats") == 0)
                stats();
            else if (strcmp(tokens[0], "rd") == 0)
                rd();
            else
                continue;
        }

        if (num_tokens == 2)
        {
            if (strcmp(tokens[0], "md") == 0)
                md(tokens[1]);
            if (strcmp(tokens[0], "cd") == 0)
                cd(tokens[1]);
            if (strcmp(tokens[0], "display") == 0)
                display(tokens[1]);
            if (strcmp(tokens[0], "create") == 0)
                create(tokens[1]);
            if (strcmp(tokens[0], "rm") == 0)
                rm(tokens[1]);
        }
    }

    return 0;
}

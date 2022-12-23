#include <iostream>
#include "fs.h"

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
    currDirIndex = 0;
    //load fat if exists
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    //creates root as a directory and writes to disk (on block 0)
    dir_entry root;
    root.file_name[0] = '/';
    root.size = 0;
    root.first_blk = 0;
    root.type = 0x00;
    root.access_rights = 0x06;
    writeDirDir(root, ROOT_BLOCK);
    
    dir_entry data[disk.get_no_blocks()];
    data[0] = root;
    for(int i = 1; i < disk.get_no_blocks(); i++){
        //if first_blk > no_blocks(), dirEntry isnt used
        data[i].first_blk = disk.get_no_blocks() + 1;
    }
    uint8_t *rawData = (uint8_t*)&data;
    disk.write(ROOT_BLOCK,rawData);
    

    //initiates FAT and writes it to disk (on block 1)
    //-1 = eof, -2 = not allocated
    fat[0] = FAT_EOF;
    fat[1] = FAT_EOF;
    for(int i = 2; i < BLOCK_SIZE/2; i++){
        fat[i] = FAT_FREE;
    }
    rawData = (uint8_t*)&fat;
    disk.write(FAT_BLOCK,rawData);

    //resets curDirIndex
    currDirIndex = 0;
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    //check names
    bool ok = checkName(filepath);
    if(!ok){
        return 1;
    }

    //gets text from input and puts into string data
    bool running = true;
    std::string line = "";
    std::string data;
    while(running){
        std::getline(std::cin, line);
        if(line == ""){
            //gets rid of newline character
            data.erase(data.size() - 1);
            running = false;
        }
        else{
            data.append(line);
            data.append("\n");
        }
    }

    //create a dirEntry
    dir_entry fileDirEntry;
    std::strcpy(fileDirEntry.file_name, filepath.c_str());
    fileDirEntry.size = data.size();
    fileDirEntry.first_blk = findEmptyFat(FAT_BLOCK);
    fileDirEntry.type = 0;
    fileDirEntry.access_rights = 0x06;
    //writes dirEntry into current directory
    writeFileDir(fileDirEntry);
    //write data into disk (updates fat)
    writeFileData(data);
    
    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::string output = getFileData(filepath);
    std::cout << output << std::endl;
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    dir_entry data[disk.get_no_blocks()];
    uint8_t *rawData = (uint8_t*)&data;
    disk.read(currDirIndex, rawData);

    int index = findEmptyBlock(data);

    std::cout << "name\tSize" << std::endl;
    for(int i = 1; i < index; i++){
        std::cout << data[i].file_name << "\t";
        std::cout << data[i].size << std::endl;
    }
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    //std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    //load dirEntries on curDirectory
    dir_entry dirData[disk.get_no_blocks()];
    uint8_t *dirDataPtr = (uint8_t*)&dirData;
    disk.read(currDirIndex, dirDataPtr);

    bool foundDir = false;
    int index = 0;
    while(index < disk.get_no_blocks() && foundDir == false){
        //look if sourcepath exists
        if(dirData[index].file_name == sourcepath){
            foundDir = true;
            index--;
        }
        //look if destpath already exists
        if(dirData[index].file_name == destpath){
            return 1;
        }
        index++;
    }
    //return if sourcepath was not found
    if(foundDir == false){
        return 1;
    }
    //copies data from sourcepath
    std::string fileData = getFileData(sourcepath);

    //create a dirEntry for the new file
    dir_entry fileDirEntry;
    std::strcpy(fileDirEntry.file_name, destpath.c_str());
    fileDirEntry.size = fileData.size();
    fileDirEntry.first_blk = findEmptyFat(FAT_BLOCK);
    fileDirEntry.type = dirData[index].type;
    fileDirEntry.access_rights = dirData[index].access_rights;
    //writes dirEntry into current directory
    writeFileDir(fileDirEntry);

    //writes copied data on new location
    writeFileData(fileData);

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    //load dirEntries on curDirectory
    dir_entry dirData[disk.get_no_blocks()];
    uint8_t *dirDataPtr = (uint8_t*)&dirData;
    disk.read(currDirIndex, dirDataPtr);

    bool foundDir = false;
    int index = 0;
    for(int i = 0; i < disk.get_no_blocks(); i++){
        //look if sourcepath exists
        if(dirData[i].file_name == sourcepath){
            foundDir = true;
            index = i;
        }
        //look if destpath already exists
        if(dirData[i].file_name == destpath){
            return 1;
        }
    }
    //return if sourcepath was not found
    if(foundDir == false){
        return 1;
    }
    //replace name on dirEntry
    std::strcpy(dirData[index].file_name, destpath.c_str());

    //update dirEntries on disk
    uint8_t *rawData = (uint8_t*)&dirData;
    disk.write(currDirIndex,rawData);

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    //load dirEntries on curDirectory
    dir_entry dirData[disk.get_no_blocks()];
    uint8_t *dirDataPtr = (uint8_t*)&dirData;
    disk.read(currDirIndex, dirDataPtr);

    bool foundDir = false;
    int index = 0;
    while(index < disk.get_no_blocks() && foundDir == false){
        //look if file2 exists
        if(dirData[index].file_name == filepath){
            if(dirData[index].first_blk != disk.get_no_blocks() + 1){
                foundDir = true;
                index--;
            }
        }
        index++;
    }
    //return if sourcepath was not found
    if(foundDir == false){
        return 1;
    }
    //sets all blocks containing current file as free
    bool done = false;
    int fatIndex = dirData[index].first_blk;
    int lastFat = 0;
    while(done == false){
        //if current index isnt EOF, jump to next and clear earlier fat
        if(fat[fatIndex] != FAT_EOF){
            lastFat = fatIndex;
            fatIndex = fat[fatIndex];
            fat[lastFat] = FAT_FREE;
        }
        //else just clear fat and end loop
        else{
            fat[fatIndex] = FAT_FREE;
            done = true;
        }
    }
    //copies everything right of dirEntry
    dir_entry rightSide[disk.get_no_blocks()];
    int tempIndex = 0;
    for(int i = index + 1; i < disk.get_no_blocks(); i++){
        rightSide[tempIndex] = dirData[i];
        tempIndex++;
    }
    //copies everything left of dirEntry
    dir_entry newDirData[disk.get_no_blocks()];
    for(int i = 0; i < index; i++){
        newDirData[i] = dirData[i];
    }
    //adds both sides together - without current dirEntry
    tempIndex = 0;
    for(int i = index; i < disk.get_no_blocks(); i++){
        newDirData[i] = rightSide[tempIndex];
        tempIndex++;
    }
    //adds one dirEntry at end to replace the removed dirEntry
    newDirData[disk.get_no_blocks() - 1].first_blk = disk.get_no_blocks() + 1;

    //writes to disk
    uint8_t *rawData = (uint8_t*)&newDirData;
    disk.write(currDirIndex,rawData);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::string file1Data = getFileData(filepath1);
    std::string file2Data = getFileData(filepath2);
    file2Data.append(file1Data);
    
    //load dirEntries on curDirectory
    dir_entry dirData[disk.get_no_blocks()];
    uint8_t *dirDataPtr = (uint8_t*)&dirData;
    disk.read(currDirIndex, dirDataPtr);

    bool foundDir = false;
    int index = 0;
    while(index < disk.get_no_blocks() && foundDir == false){
        //look if file2 exists
        if(dirData[index].file_name == filepath2){
            if(dirData[index].first_blk != disk.get_no_blocks() + 1){
                foundDir = true;
                index--;
            }
        }
        index++;
    }
    //return if sourcepath was not found
    if(foundDir == false){
        return 1;
    }
    rm(filepath2);
    //update new dirEntry
    //std::cout << "Index: " << index << std::endl;
    dirData[index].size = file2Data.size();
    //write dirEntry and data
    writeFileDir(dirData[index]);
    writeFileData(file2Data);

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}

//Writes dir into first empty block
void
FS::writeFileDir(dir_entry directory){
    dir_entry data[disk.get_no_blocks()];
    uint8_t *rawData = (uint8_t*)&data;
    disk.read(currDirIndex, rawData);
    
    int index = findEmptyBlock(data);
    //puts dirEntry into current dir
    data[index] = directory;
    disk.write(currDirIndex, rawData);
}

//todo
void
FS::writeDirDir(dir_entry directory, int block = -1){
    dir_entry data[disk.get_no_blocks()];
    data[0] = directory;

    uint8_t *rawData = (uint8_t*)&data;
    if(block == ROOT_BLOCK){
        disk.write(ROOT_BLOCK,rawData);
    }
    else{
    }
}

//writes data into disk
void
FS::writeFileData(std::string data){
    //copies the string into a char array
    char dataCharArray[data.size()];
    std::strcpy(dataCharArray, data.c_str()); 

    //writes to disk and updates fat
    if(sizeof(dataCharArray) > BLOCK_SIZE){
        //amount of blocks needed
        int blockAmount = ceil(sizeof(dataCharArray) / (float)BLOCK_SIZE);
        int buffer = 0;

        //puts dataCharArray into multiple arrays in multiDimArray
        char multiDimArray[blockAmount][BLOCK_SIZE];
        for(int i = 0; i < blockAmount; i++){
            buffer = (BLOCK_SIZE * i);
            for(int dataIndex = 0; dataIndex < BLOCK_SIZE; dataIndex++){
                multiDimArray[i][dataIndex] = dataCharArray[dataIndex + buffer];
            }
        }
        //writes into disk and fat
        uint8_t *rawData;
        int fatIndex = findEmptyFat(FAT_BLOCK);
        int lastFat = 0;
        
        rawData = (uint8_t*)&multiDimArray[0];
        disk.write(fatIndex, rawData);
        fat[fatIndex] = EOF;

        lastFat = fatIndex;
        //goes through all other blocks, puts fatIndex of current block
        //into the previous block, sets own to EOF.
        for(int i = 1; i < blockAmount; i++){
            fatIndex = findEmptyFat(FAT_BLOCK);
            rawData = (uint8_t*)&multiDimArray[i];

            disk.write(fatIndex, rawData);
            fat[lastFat] = fatIndex;
            fat[fatIndex] = EOF;
            lastFat = fatIndex;
        }
    }
    else{
        //if file fits into one block, simply write to block
        int fatIndex = findEmptyFat(FAT_BLOCK);

        uint8_t *rawData = (uint8_t*)&dataCharArray;
        disk.write(findEmptyFat(fatIndex), rawData);
        fat[findEmptyFat(fatIndex)] = EOF;
    }
}

//gets data from disk and returns it as a string
std::string
FS::getFileData(std::string filepath){
    //get dirEntries from curDirectory
    dir_entry dirData[disk.get_no_blocks()];
    uint8_t *dirDataPtr = (uint8_t*)&dirData;
    disk.read(currDirIndex, dirDataPtr);

    char data[BLOCK_SIZE];
    uint8_t *rawData = (uint8_t*)&data;

    std::string temp;
    std::string temp2;
    std::string returnString;
    int index = 0;
    int fatIndex = 0;
    bool eof = false;
    //search dirEntries for file
    for(int i = 0; i < disk.get_no_blocks(); i++){
        //file found
        if(dirData[i].file_name == filepath){
            fatIndex = dirData[i].first_blk;
            //goes through fat to get all data
            while(eof == false){
                disk.read(fatIndex, rawData);
                temp = data;
                temp2 = "";
                //to avoid garbage at end of string
                if(temp.size() > BLOCK_SIZE){
                    for(int i = 0; i < BLOCK_SIZE; i++){
                        temp2 += (temp.at(i));
                    }
                    returnString.append(temp2);
                } else{
                    returnString.append(temp);
                } 
                
                if(fat[fatIndex] == EOF){
                    eof = true;
                } else{
                    fatIndex = fat[fatIndex];
                }
            }
            return returnString;
        }
    }
    //std::cout << "File does not exist" << std::endl;
    return "";
}

//finds first unused block
//(sometimes gets garbage files when writing)
int
FS::findEmptyBlock(dir_entry data[]){
    //index = 1 (0 points to current directory)
    int index = 1;
    bool found = false;
    //finds first empty block 
    while(index < disk.get_no_blocks() && !found){
        std::string temp = data[index].file_name;
        for(int i = 0; i < temp.size(); i++){
            if(!isalnum(temp[i])){
                found = true;
                return index;
            }
        }
        if(data[index].first_blk > disk.get_no_blocks()){
            found = true;
            return index;
        } else if(!(int)data[index].type == 0 && !(int)data[index].type == 1){
            found = true;
            return index;
        }
        index++;
    }
    return -1;
}

//finds first unusedblock in fat 
//returns -1 if no free blocks are found
int
FS::findEmptyFat(int startIndex = 2){
    int fatSize = BLOCK_SIZE/2;
    int index = startIndex;

    while(index < fatSize){
        if(fat[index] == FAT_FREE){
            return index;
        }
        index++;
    }
    return -1;
}

//checks if filename is ok
bool
FS::checkName(std::string filepath){
    try{
        if(filepath.size() > 56){
            throw std::overflow_error("filename too long");
        }
        dir_entry nameData[disk.get_no_blocks()];
        uint8_t *nameDataptr = (uint8_t*)&nameData;
        disk.read(currDirIndex, nameDataptr);
        for(int i = 0; i < disk.get_no_blocks(); i++){
            if(nameData[i].file_name == filepath){
                //first_blk no_blocks + 1 means file isnt used
                if(nameData[i].first_blk != disk.get_no_blocks() + 1){
                    throw std::invalid_argument("filename already exists");
                }
            }
        }
    }
    catch(const std::invalid_argument& e){
        //std::cout << "fileName already exists!" << std::endl;
        return false;
    }
    catch(const std::overflow_error& e){
        //std::cout << "fileName too long" << std::endl;
        return false;
    }
    return true;
}

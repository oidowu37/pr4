#ifndef _DFSLIB_SHARED_H
#define _DFSLIB_SHARED_H

#include <algorithm>
#include <cctype>
#include <locale>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

#include "src/dfs-utils.h"
#include "proto-src/dfs-service.grpc.pb.h"

#define DFS_RESET_TIMEOUT 2000
#define DFS_CHUNK_SIZE 4096  // 4KB chunks for file streaming

//
// STUDENT INSTRUCTION:
//
// Add your additional code here
//

/**
 * Get the file size for a given file path
 * Returns -1 if file doesn't exist
 */
inline int64_t GetFileSize(const std::string& filepath) {
    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) == 0) {
        return file_stat.st_size;
    }
    return -1;
}

/**
 * Get the modification time for a given file path
 * Returns -1 if file doesn't exist
 */
inline int32_t GetFileModTime(const std::string& filepath) {
    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) == 0) {
        return (int32_t)file_stat.st_mtime;
    }
    return -1;
}

/**
 * Get the creation/change time for a given file path
 * Returns -1 if file doesn't exist
 */
inline int32_t GetFileCreateTime(const std::string& filepath) {
    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) == 0) {
        return (int32_t)file_stat.st_ctime;
    }
    return -1;
}

#endif
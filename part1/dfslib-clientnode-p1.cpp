#include <regex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>

#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using dfs_service::FileChunk;
using dfs_service::FileName;
using dfs_service::FileStatus;
using dfs_service::FileList;
using dfs_service::Empty;

//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {
    // Default server address
    std::string server_address = "localhost:50051";
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    this->CreateStub(channel);
}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the client
    // StatusCode::CANCELLED otherwise
    //

    // Get the full path to the file from mount point
    std::string filepath = this->MountPath() + filename;
    
    dfs_log(LL_DEBUG) << "Storing file: " << filepath;
    
    // Check if file exists
    std::ifstream infile(filepath, std::ios::binary);
    if (!infile.is_open()) {
        dfs_log(LL_ERROR) << "Could not open file for reading: " << filepath;
        return StatusCode::NOT_FOUND;
    }
    
    ClientContext context;
    // Set a deadline for this RPC call
    std::chrono::system_clock::time_point deadline = 
        std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout);  // ✅ Use this->deadline_timeout
    context.set_deadline(deadline);
    
    FileStatus response;
    
    // Create a writer for streaming
    auto writer = this->service_stub->Store(&context, &response);
    
    // Read file in chunks and stream to server
    char buffer[DFS_CHUNK_SIZE];
    FileChunk chunk;
    chunk.set_filename(filename);
    
    while (infile.read(buffer, DFS_CHUNK_SIZE) || infile.gcount() > 0) {
        chunk.set_data(buffer, infile.gcount());
        if (!writer->Write(chunk)) {
            dfs_log(LL_ERROR) << "Failed to write chunk to server";
            infile.close();
            return StatusCode::CANCELLED;
        }
    }
    
    infile.close();
    
    // Close the writer and get the response
    writer->WritesDone();
    Status status = writer->Finish();
    
    if (!status.ok()) {
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            dfs_log(LL_ERROR) << "Deadline exceeded for store operation";
            return StatusCode::DEADLINE_EXCEEDED;
        }
        dfs_log(LL_ERROR) << "Store failed: " << status.error_message();
        return StatusCode::CANCELLED;
    }
    
    dfs_log(LL_DEBUG) << "File stored successfully: " << filename;
    return StatusCode::OK;
}


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    // Set a deadline for this RPC call
    std::chrono::system_clock::time_point deadline = 
        std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout);  // ✅ Use this->deadline_timeout
    context.set_deadline(deadline);
    
    FileName request;
    request.set_name(filename);
    
    dfs_log(LL_DEBUG) << "Fetching file: " << filename;
    
    // Open file for writing
    std::string filepath = this->MountPath() + filename;
    std::ofstream outfile(filepath, std::ios::binary);
    if (!outfile.is_open()) {
        dfs_log(LL_ERROR) << "Could not open file for writing: " << filepath;
        return StatusCode::CANCELLED;
    }
    
    // Create a reader for streaming
    auto reader = this->service_stub->Fetch(&context, request);
    
    FileChunk chunk;
    while (reader->Read(&chunk)) {
        if (!chunk.data().empty()) {
            outfile.write(chunk.data().data(), chunk.data().size());
        }
    }
    
    outfile.close();
    
    Status status = reader->Finish();
    if (!status.ok()) {
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            dfs_log(LL_ERROR) << "Deadline exceeded for fetch operation";
            return StatusCode::DEADLINE_EXCEEDED;
        }
        if (status.error_code() == StatusCode::NOT_FOUND) {
            dfs_log(LL_ERROR) << "File not found on server: " << filename;
            return StatusCode::NOT_FOUND;
        }
        dfs_log(LL_ERROR) << "Fetch failed: " << status.error_message();
        return StatusCode::CANCELLED;
    }
    
    dfs_log(LL_DEBUG) << "File fetched successfully: " << filename;
    return StatusCode::OK;
}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //

    ClientContext context;
    // Set a deadline for this RPC call
    std::chrono::system_clock::time_point deadline = 
        std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout);  // ✅ Use this->deadline_timeout
    context.set_deadline(deadline);
    
    FileName request;
    request.set_name(filename);
    
    FileStatus response;
    
    dfs_log(LL_DEBUG) << "Deleting file: " << filename;
    
    Status status = this->service_stub->Delete(&context, request, &response);
    
    if (!status.ok()) {
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            dfs_log(LL_ERROR) << "Deadline exceeded for delete operation";
            return StatusCode::DEADLINE_EXCEEDED;
        }
        if (status.error_code() == StatusCode::NOT_FOUND) {
            dfs_log(LL_ERROR) << "File not found on server: " << filename;
            return StatusCode::NOT_FOUND;
        }
        dfs_log(LL_ERROR) << "Delete failed: " << status.error_message();
        return StatusCode::CANCELLED;
    }
    
    dfs_log(LL_DEBUG) << "File deleted successfully: " << filename;
    return StatusCode::OK;

}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
        ClientContext context;
    // Set a deadline for this RPC call
    std::chrono::system_clock::time_point deadline = 
        std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout);  // ✅ Use this->deadline_timeout
    context.set_deadline(deadline);
    
    Empty request;
    FileList response;
    
    dfs_log(LL_DEBUG) << "Listing files from server";
    
    Status status = service_stub->List(&context, request, &response);
    
    if (!status.ok()) {
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            dfs_log(LL_ERROR) << "Deadline exceeded for list operation";
            return StatusCode::DEADLINE_EXCEEDED;
        }
        dfs_log(LL_ERROR) << "List failed: " << status.error_message();
        return StatusCode::CANCELLED;
    }
    
    // Fill the file_map with results
    if (file_map != nullptr) {
        file_map->clear();
        for (const auto& file_info : response.files()) {
            (*file_map)[file_info.name()] = file_info.mtime();
            dfs_log(LL_DEBUG) << "  Listed file: " << file_info.name() << " (mtime: " << file_info.mtime() << ")";
        }
    }
    
    // Optionally display the listing
    if (display) {
        std::cout << "File Listing:" << std::endl;
        for (const auto& file_info : response.files()) {
            std::cout << "  " << file_info.name() << " (mtime: " << file_info.mtime() << ")" << std::endl;
        }
    }
    
    dfs_log(LL_DEBUG) << "File listing retrieved successfully";
    return StatusCode::OK;
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    
    ClientContext context;
    // Set a deadline for this RPC call
    std::chrono::system_clock::time_point deadline = 
        std::chrono::system_clock::now() + std::chrono::milliseconds(this->deadline_timeout);  // ✅ Use this->deadline_timeout
    context.set_deadline(deadline);
    
    FileName request;
    request.set_name(filename);
    
    FileStatus response;
    
    dfs_log(LL_DEBUG) << "Getting status for file: " << filename;
    
    Status status = this->service_stub->Stat(&context, request, &response);
    
    if (!status.ok()) {
        if (status.error_code() == StatusCode::DEADLINE_EXCEEDED) {
            dfs_log(LL_ERROR) << "Deadline exceeded for stat operation";
            return StatusCode::DEADLINE_EXCEEDED;
        }
        if (status.error_code() == StatusCode::NOT_FOUND) {
            dfs_log(LL_ERROR) << "File not found on server: " << filename;
            return StatusCode::NOT_FOUND;
        }
        dfs_log(LL_ERROR) << "Stat failed: " << status.error_message();
        return StatusCode::CANCELLED;
    }
    
    // If a file_status pointer is provided, copy the response to it
    if (file_status != nullptr) {
        FileStatus* status_ptr = static_cast<FileStatus*>(file_status);
        status_ptr->CopyFrom(response);
    }
    
    dfs_log(LL_DEBUG) << "Status retrieved for file: " << filename;
    dfs_log(LL_DEBUG) << "  Size: " << response.size() << " bytes";
    dfs_log(LL_DEBUG) << "  Mtime: " << response.mtime();
    dfs_log(LL_DEBUG) << "  Ctime: " << response.ctime();
    
    return StatusCode::OK;
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//



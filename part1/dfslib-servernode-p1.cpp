#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>

#include "src/dfs-utils.h"
#include "dfslib-shared-p1.h"
#include "dfslib-servernode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using dfs_service::DFSService;


//
// STUDENT INSTRUCTION:
//
// DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in the `dfs-service.proto` file.
//
// You should add your definition overrides here for the specific
// methods that you created for your GRPC service protocol. The
// gRPC tutorial described in the readme is a good place to get started
// when trying to understand how to implement this class.
//
// The method signatures generated can be found in `proto-src/dfs-service.grpc.pb.h` file.
//
// Look for the following section:
//
//      class Service : public ::grpc::Service {
//
// The methods returning grpc::Status are the methods you'll want to override.
//
// In C++, you'll want to use the `override` directive as well. For example,
// if you have a service method named MyMethod that takes a MyMessageType
// and a ServerWriter, you'll want to override it similar to the following:
//
//      Status MyMethod(ServerContext* context,
//                      const MyMessageType* request,
//                      ServerWriter<MySegmentType> *writer) override {
//
//          /** code implementation here **/
//      }
//
class DFSServiceImpl final : public DFSService::Service {

private:

    /** The mount path for the server **/
    std::string mount_path;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }


public:

    DFSServiceImpl(const std::string &mount_path): mount_path(mount_path) {
    }

    ~DFSServiceImpl() {}

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // implementations of your protocol service methods
    //

    /*
     * Store: Receive file chunks from client and write to disk
     */
    Status Store(ServerContext* context,
                ServerReader<dfs_service::FileChunk>* reader,
                dfs_service::FileStatus* response) override {

        dfs_service::FileChunk chunk;
        std::string filename;
        std::ofstream outfile;

        // Read all chunks and write to file
        while (reader->Read(&chunk)){
            if (context->IsCancelled()) {
                if (outfile.is_open()) {
                    outfile.close();
                }
                return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
            }

            if (filename.empty()){
                filename = chunk.filename();
                std::string full_path = WrapPath(filename);

                // Open file for writing (binary mode)
                outfile.open(full_path, std::ios::binary);
                if (!outfile.is_open()){
                    dfs_log(LL_ERROR) << "Could not open file: " << full_path;
                    return Status(StatusCode::INTERNAL, "Could not open file for writing");
                }
                dfs_log(LL_DEBUG) << "Storing file: " << full_path;
            }

            // Write chunk to file
            if (!chunk.data().empty()){
                outfile.write(chunk.data().data(), chunk.data().size());
            }
        }

        if (outfile.is_open()){
            outfile.close();
        }


        // return file status
        std::string full_path = WrapPath(filename);
        response->set_filename(filename);
        response->set_size(GetFileSize(full_path));
        response->set_mtime(GetFileModTime(full_path));
        response->set_ctime(GetFileCreateTime(full_path));

        dfs_log(LL_DEBUG) << "File stored successfully: " << filename;
        return grpc::Status::OK;
    }

    /*
     * Fetch: Read file from disk and stream chunks to client
     */
    Status Fetch(ServerContext* context,
                 const dfs_service::FileName* request,
                 ServerWriter<dfs_service::FileChunk>* writer) override {
        
        std::string filename = request->name();
        std::string full_path = WrapPath(filename);
        
        dfs_log(LL_DEBUG) << "Fetching file: " << full_path;
        
        // Check if file exists
        std::ifstream infile(full_path, std::ios::binary);
        if (!infile.is_open()) {
            dfs_log(LL_ERROR) << "Could not open file: " << full_path;
            return Status(StatusCode::NOT_FOUND, "File not found");
        }
        
        // Check for deadline exceeded
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        
        // Read file in chunks and stream to client
        char buffer[DFS_CHUNK_SIZE];
        dfs_service::FileChunk chunk;
        chunk.set_filename(filename);
        
        while (infile.read(buffer, DFS_CHUNK_SIZE) || infile.gcount() > 0) {
            chunk.set_data(buffer, infile.gcount());
            if (!writer->Write(chunk)) {
                dfs_log(LL_ERROR) << "Failed to write chunk";
                return Status(StatusCode::INTERNAL, "Failed to write chunk");
            }
            chunk.clear_data();
        }
        
        infile.close();
        
        dfs_log(LL_DEBUG) << "File fetched successfully: " << filename;
        return grpc::Status::OK;
    }

        /**
     * Delete: Remove a file from the server
     */
    Status Delete(ServerContext* context,
                  const dfs_service::FileName* request,
                  dfs_service::FileStatus* response) override {
        
        std::string filename = request->name();
        std::string full_path = WrapPath(filename);
        
        dfs_log(LL_DEBUG) << "Deleting file: " << full_path;
        
        // Check for deadline exceeded
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        
        // Try to delete the file
        if (std::remove(full_path.c_str()) != 0) {
            dfs_log(LL_ERROR) << "Could not delete file: " << full_path;
            return Status(StatusCode::NOT_FOUND, "File not found");
        }
        
        response->set_filename(filename);
        dfs_log(LL_DEBUG) << "File deleted successfully: " << filename;
        return grpc::Status::OK;
    }

        /**
     * List: Return list of all files in the mount directory
     */
    Status List(ServerContext* context,
                const dfs_service::Empty* request,
                dfs_service::FileList* response) override {
        
        dfs_log(LL_DEBUG) << "Listing files in: " << mount_path;
        
        // Check for deadline exceeded
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        
        DIR* dir = opendir(mount_path.c_str());
        if (!dir) {
            dfs_log(LL_ERROR) << "Could not open directory: " << mount_path;
            return Status(StatusCode::INTERNAL, "Could not open directory");
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip . and .. entries
            if (entry->d_type == DT_REG) {  // Regular file only
                std::string filename = entry->d_name;
                std::string full_path = WrapPath(filename);
                
                auto file_info = response->add_files();
                file_info->set_name(filename);
                file_info->set_mtime(GetFileModTime(full_path));
                
                dfs_log(LL_DEBUG) << "  Listed file: " << filename;
            }
        }
        
        closedir(dir);
        dfs_log(LL_DEBUG) << "File listing complete";
        return grpc::Status::OK;
    }

        /**
     * Stat: Return file attributes/status
     */
    Status Stat(ServerContext* context,
                const dfs_service::FileName* request,
                dfs_service::FileStatus* response) override {
        
        std::string filename = request->name();
        std::string full_path = WrapPath(filename);
        
        dfs_log(LL_DEBUG) << "Getting status for: " << full_path;
        
        // Check for deadline exceeded
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        
        // Check if file exists
        if (GetFileSize(full_path) == -1) {
            dfs_log(LL_ERROR) << "File not found: " << full_path;
            return Status(StatusCode::NOT_FOUND, "File not found");
        }
        
        response->set_filename(filename);
        response->set_size(GetFileSize(full_path));
        response->set_mtime(GetFileModTime(full_path));
        response->set_ctime(GetFileCreateTime(full_path));
        
        dfs_log(LL_DEBUG) << "Status retrieved for: " << filename;
        return grpc::Status::OK;
    }

};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly,
// but be aware that the testing environment is expecting these three
// methods as-is.
//
/**
 * The main server node constructor
 *
 * @param server_address
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        std::function<void()> callback) :
    server_address(server_address), mount_path(mount_path), grader_callback(callback) {}

/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
    this->server->Shutdown();
}

/** Server start **/
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path);
    ServerBuilder builder;
    builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    this->server->Wait();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional DFSServerNode definitions here
//

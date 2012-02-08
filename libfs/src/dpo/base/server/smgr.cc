#include "dpo/base/server/smgr.h"
#include "dpo/base/common/storage_protocol.h"


namespace dpo {
namespace server {


//
// Group Containers per (type, ACL) 
//
//

class ContainersPerClient {
public:
	// verify_allocation (capability)
	// 
	

private:
	// index of available containers to client 


};


StorageManager::StorageManager(rpcs* rpc_server)
	: rpc_server_(rpc_server)
{ }


int
StorageManager::Init()
{
    rpc_server_->reg(::dpo::StorageProtocol::kAllocateContainerVector, this, 
	                 &::dpo::server::StorageManager::AllocateContainerVector);

}




#if 0

// Makes the OS dependent call to allocate space
int
StorageManager::CreateExtent(int acl)
{
	

}


int
StorageManager::CreateStorageContainer()
{


}


int 
StorageManager::AllocateContainerInternal(int clt, int type, int acl)
{


}



int 
StorageManager::AllocateContainer(int clt, int type, int acl, int n)
{


}

#endif

int 
StorageManager::AllocateContainerVector(int clt, std::vector< ::dpo::StorageProtocol::ContainerRequest> container_request_vector, int& result)
{


}


} // namespace server
} // namespace dpo
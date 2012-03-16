#include "ssa/main/server/ssa.h"
#include "common/errno.h"
#include "common/debug.h"
#include "ssa/main/server/salloc.h"
#include "ssa/main/server/hlckmgr.h"
#include "ssa/main/server/registry.h"
#include "ssa/containers/super/container.h"
#include "ssa/containers/name/container.h"
#include "ssa/containers/set/container.h"
#include "spa/pool/pool.h"
#include "spa/const.h"

namespace ssa {
namespace server {


int
StorageSystem::Init()
{
	if ((hlckmgr_ = new ::ssa::cc::server::HLockManager(ipc_)) == NULL) {
		return -E_NOMEM;
	}
	hlckmgr_->Init();
	if ((salloc_ = new StorageAllocator(ipc_)) == NULL) {
		delete hlckmgr_;
		return -E_NOMEM;
	}
	salloc_->Init();
	if ((registry_ = new Registry(ipc_)) == NULL) {
		delete salloc_;
		delete hlckmgr_;
	}
	registry_->Init();
	return E_SUCCESS;
}


int
StorageSystem::Load(const char* source, unsigned int flags)
{
	int                ret;
	void*              b;
	::server::Session* session = NULL; // we need no journaling
	
	if ((ret = StoragePool::Open(source, &pool_)) < 0) {
		return ret;
	}
	if ((b = pool_->root()) == 0) {
		return -E_NOENT;
	}
	if ((super_obj_ = ssa::containers::server::SuperContainer::Object::Load(b)) == NULL) {
		return -E_NOMEM;
	}
	if ((ret = salloc_->Load(pool_)) < 0) {
		return ret;
	}
	return E_SUCCESS;
}


// formats the header of the pool to enable a storage allocator 
// on top of it. it prepares the super_obj so that the storage
// system can continue formatting.
int 
StorageSystem::Make(const char* target, unsigned int flags)
{
	ssa::containers::server::SuperContainer::Object*                      super_obj;
	ssa::containers::server::NameContainer::Object*                       root_obj;
	ssa::containers::server::SetContainer<ssa::common::ObjectId>::Object* set_obj;
	int                                                                   ret;
	char*                                                                 b;
	char*                                                                 buffer;
	SsaSession*                                                           session = NULL; // we need no journaling and storage allocator
	size_t                                                                master_extent_size;

	
	if ((ret = StoragePool::Open(target, &pool_)) < 0) {
		return ret;
	}
	
	// 1) create the superblock object/proxy,
	// 2) create the directory inode objext/proxy and set the root 
	//    of the superblock to point to the new directory inode.
	
	master_extent_size = 0;
	master_extent_size += sizeof(ssa::containers::server::NameContainer::Object);
	master_extent_size += sizeof(ssa::containers::server::SuperContainer::Object);
	master_extent_size += sizeof(ssa::containers::server::SetContainer<ssa::common::ObjectId>::Object);
	if ((ret = pool_->AllocateExtent(master_extent_size, (void**) &buffer)) < 0) {
		return ret;
	}
	pool_->set_root((void*) buffer);

	// superblock 
	b = buffer;
	if ((super_obj = ssa::containers::server::SuperContainer::Object::Make(session, b)) == NULL) {
		return -E_NOMEM;
	}
	// root directory inode
	b += sizeof(ssa::containers::server::SuperContainer::Object);
	if ((root_obj = ssa::containers::server::NameContainer::Object::Make(session, b)) == NULL) {
		return -E_NOMEM;
	}
	super_obj->set_root(session, root_obj->oid());
	// storage container
	b += sizeof(ssa::containers::server::NameContainer::Object);
	if ((set_obj = ssa::containers::server::SetContainer<ssa::common::ObjectId>::Object::Make(session, b)) == NULL) {
		return -E_NOMEM;
	}
	super_obj->set_freelist(session, set_obj->oid());
	b += sizeof(ssa::containers::server::SetContainer<ssa::common::ObjectId>::Object);
	assert(b < buffer + master_extent_size + 1); 
	return E_SUCCESS;


}


int
StorageSystem::Close()
{
	int ret;

	if ((ret = StoragePool::Close(pool_)) < 0) {
		return ret;
	}
	pool_ = NULL;
	return E_SUCCESS;
}


StorageSystem::~StorageSystem()
{
	delete registry_;
	delete salloc_;
	delete hlckmgr_;
}

} // namespace server
} // namespace ssa
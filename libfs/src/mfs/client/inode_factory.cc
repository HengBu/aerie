#include "mfs/client/inode_factory.h"
#include "common/debug.h"
#include "client/backend.h"
#include "ssa/containers/typeid.h"
#include "mfs/client/dir_inode.h"
#include "mfs/client/file_inode.h"

namespace mfs {
namespace client {

pthread_mutex_t InodeFactory::mutex_ = PTHREAD_MUTEX_INITIALIZER;;

int
InodeFactory::LoadDirInode(::client::Session* session, 
                           ::ssa::common::ObjectId oid, 
                           ::client::Inode** ipp)
{
	int                                ret = E_SUCCESS;
	ssa::common::ObjectProxyReference* ref;
	DirInode*                          dip;

	// atomically get a reference to the persistent object and 
	// create the in-core Inode 
	pthread_mutex_lock(&mutex_);
	if ((ret = session->omgr_->FindObject(session, oid, &ref)) == E_SUCCESS) {
		if (ref->owner()) {
			// the in-core inode already exists; just return this and 
			// we are done
			dip = reinterpret_cast<DirInode*>(ref->owner());
		} else {
			dip = new DirInode(ref);
		}
	} else {
		dip = new DirInode(ref);
	}
	pthread_mutex_unlock(&mutex_);
	*ipp = dip;
	return E_SUCCESS;
}


int
InodeFactory::MakeDirInode(::client::Session* session, ::client::Inode** ipp)
{
	int                                               ret = E_SUCCESS;
	ssa::containers::client::NameContainer::Object*   obj;

	if ((obj = ssa::containers::client::NameContainer::Object::Make(session)) == NULL) {
		return -E_NOMEM;
	}
	if ((ret = LoadDirInode(session, obj->oid(), ipp)) < 0) {
		// FIXME: deallocate the allocated persistent object (container)
		return ret;
	}
	return ret;
}


int
InodeFactory::LoadFileInode(::client::Session* session, 
                            ::ssa::common::ObjectId oid, 
                            ::client::Inode** ipp)
{
	
	int                                ret = E_SUCCESS;
	ssa::common::ObjectProxyReference* ref;
	FileInode*                         fip;

	// atomically get a reference to the persistent object and 
	// create the in-core Inode 
	pthread_mutex_lock(&mutex_);
	if ((ret = session->omgr_->FindObject(session, oid, &ref)) == E_SUCCESS) {
		if (ref->owner()) {
			// the in-core inode already exists; just return this and 
			// we are done
			fip = reinterpret_cast<FileInode*>(ref->owner());
		} else {
			fip = new FileInode(ref);
		}
	} else {
		fip = new FileInode(ref);
	}
	pthread_mutex_unlock(&mutex_);
	*ipp = fip;
	return ret;
}


int
InodeFactory::MakeFileInode(::client::Session* session, ::client::Inode** ipp)
{
	int                                               ret = E_SUCCESS;
	ssa::containers::client::ByteContainer::Object*   obj;

	if ((obj = ssa::containers::client::ByteContainer::Object::Make(session)) == NULL) {
		return -E_NOMEM;
	}
	if ((ret = LoadFileInode(session, obj->oid(), ipp)) < 0) {
		// FIXME: deallocate the allocated object
		return ret;
	}
	return ret;
}


int
InodeFactory::MakeInode(::client::Session* session, int type, ::client::Inode** ipp)
{
	int ret = E_SUCCESS;

	switch (type) {
		case ::client::type::kDirInode:
			ret = MakeDirInode(session, ipp);
			break;
		case ::client::type::kFileInode:	
			ret = MakeFileInode(session, ipp);
			break;
		default:
			dbg_log (DBG_CRITICAL, "Unknown inode type\n");
	}
	return ret;
}


int 
InodeFactory::LoadInode(::client::Session* session, ssa::common::ObjectId oid, 
                        ::client::Inode** ipp)
{
	int ret = E_SUCCESS;

	switch (oid.type()) {
		case ssa::containers::T_NAME_CONTAINER: // directory
			ret = LoadDirInode(session, oid, ipp);
			break;
		case ssa::containers::T_BYTE_CONTAINER:
			ret = LoadFileInode(session, oid, ipp);
			break;
		default: 
			dbg_log (DBG_CRITICAL, "Unknown container type\n");
	}
	
	return ret;
}


int
InodeFactory::Make(::client::Session* session, int type, ::client::Inode** ipp)
{
	return MakeInode(session, type, ipp);
}


int 
InodeFactory::Load(::client::Session* session, ssa::common::ObjectId oid, 
                   ::client::Inode** ipp)
{
	return LoadInode(session, oid, ipp);
}

} // namespace client 
} // namespace mfs

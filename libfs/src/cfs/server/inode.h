#ifndef __STAMNOS_CFS_SERVER_INODE_H
#define __STAMNOS_CFS_SERVER_INODE_H

#include "osd/containers/name/container.h"
#include "osd/containers/byte/container.h"
#include "osd/main/common/obj.h"
#include "cfs/server/const.h"
#include "cfs/server/session.h"
#include "cfs/common/types.h"

namespace server {

class Inode {
public:
	Inode()
		: oid_(osd::common::ObjectId(0)),
		  ino_(0),
		  type_(0)
	{ }
	
	Inode(InodeNumber ino, int type)
		: oid_(osd::common::ObjectId(ino)),
		  ino_(ino),
		  type_(type)
	{ }
	
	static int type(InodeNumber ino) {
		osd::common::ObjectId oid(ino);
		osd::common::Object*  obj = osd::common::Object::Load(oid);
		switch (obj->type()) {
			case osd::containers::T_NAME_CONTAINER:
				return kDirInode;
			case osd::containers::T_BYTE_CONTAINER:
				return kFileInode;
		}
		return -1;
	}

	int type() { return type_; }
	InodeNumber ino() { return ino_; }

	osd::common::ObjectId oid() { return oid_; }

	osd::common::ObjectId oid_;
	InodeNumber           ino_;
	int                   type_;
};


template<typename T>
class InodeT: public Inode {
public:
	InodeT()
	{ }

	InodeT(InodeNumber ino, int type)
		: Inode(ino, type)
	{ }

	// construct inode over existing persistent persistent object into ip
	static T* Load(Session* session, InodeNumber ino, T* ip) {
		return new(ip) T(ino);
	}
	
};


} // namespace server

#endif // __STAMNOS_CFS_SERVER_INODE_H

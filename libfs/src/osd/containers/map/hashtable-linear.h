#ifndef __STAMNOS_OSD_COMMON_HASHTABLE_H 
#define __STAMNOS_OSD_COMMON_HASHTABLE_H 

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <typeinfo>
#include "bcs/main/common/cdebug.h"
#include "common/errno.h"
#include "common/hash.h"
#include "common/prof.h"
#include "scm/scm/model.h"

//#define PROFILER_SAMPLE __PROFILER_SAMPLE


//TODO: allow expansion. Instead of having a statically defined table size, allow
//having a table of pointers to pages that are dynamically allocated



enum {
	HT_REHASH = 1
};


inline uint64_t lgl2lnr (uint64_t logical_addr)
{
	return logical_addr;
}


class SplitPredicate {
public:
	virtual bool operator() (const char* key, int key_size) const = 0;
};


//FIXME: Currently we can't support PAGE_SIZE larger than 128B because of the 
//payload field size (7bits). This increases internal fragmentation because
//the smaller extent size the extent allocator can give us is 4KB (protection granularity)
const int PAGE_SIZE = 128;
const int TAG_SIZE = 1;
const int FACTOR = 1;
const int VAL_SIZE = sizeof(uint64_t);

//const int NUM_BUCKETS = 128;
const int NUM_BUCKETS = 1024;
//const int NUM_BUCKETS = 16384;

// PCM is treated as volatile??? this should be guaranteed by the PCM assembly macros
#define volatile 


// The code here assumes a LITTLE ENDIAN machine
//
// BUCKET
// ======
//
// +---+---------+---+--------...---+----+
// |TAG| PAYLOAD |TAG|              |NEXT|
// +---+---------+---+--------...---+----+
//
// TAG
// ===
// 
// if free (A==0):
// 
// +-+--------+
// |A|AVLSPACE|
// +-+--------+
//
//   A:        alloc flag
//   AVLSPACE: available space (7 bits)
//
// if alloc (A==1):
//
// +-+------+------+
// |A|KEYSZ |VALSZ | 
// +-+------+------+
//
//   A:     alloc flag (1 bit)
//   KEYSZ: key size (7 bits)
//   VALSZ: val size (0 bits)



template<typename Session>
class Entry {
public:
	// Creates an entry at address b (entry is initialized)
	static inline Entry* Make(volatile char* b, bool free, int payload_size) {
		Entry* e = (Entry*) b;
		return (e->Init(free, payload_size) == 0) ? e : NULL;
	}
	// Loads the entry located at address b
	static inline Entry* Load(volatile char* b) {
		return (Entry*) b;
	}
	// Splits the entry into two entries: [base, base+offset) and [base+offset,base+sizeof(entry))
	static inline Entry* Split(Session* session, Entry* entry, int offset) {
		Entry*          new_entry;
		volatile char*  b;
		int             new_payload_size;

		if (!entry->IsFree() ||
		    offset > entry->get_payload_size() - TAG_SIZE + 1) 
		{
			return NULL;
		}
		b = entry->tag_;
		new_entry = (Entry*) &b[offset];
		new_payload_size = entry->get_payload_size() - (offset - TAG_SIZE) - TAG_SIZE;
		new_entry->set_free(session, new_payload_size);
		return new_entry;
	}
	int Init(bool free, int payload_size) {
		assert(payload_size < (TAG_SIZE << 7));
		char   tag_alloc_bit = (free==true) ? 0x00 : 0x80;
		tag_[0] = tag_alloc_bit | payload_size;
		return 0;
	}
	bool IsFree() {
		return (tag_[0] & 0x80 ? false : true);
	}
	void set_free(Session* session) {
		tag_[0] &= 0x7F;
	}
	void set_free(Session* session, int payload_size) {
		assert(payload_size < (TAG_SIZE << 7)); 
		session->journal()->Store(&tag_[0], (char) payload_size);
		tag_[0] = payload_size;
	}
	int get_size() { 
		return (tag_[0] & 0x7F) + TAG_SIZE; 
	}
	int get_payload_size() { 
		return tag_[0] & 0x7F; 
	}
	void set_size(Session* session, int size) { 
		assert(size-TAG_SIZE < (TAG_SIZE << 7)); 
		tag_[0] = (tag_[0] & 0x80) | (size-TAG_SIZE); 
	}
	void set_payload_size(Session* session, int size) { 
		assert(size < (TAG_SIZE << 7)); 
		tag_[0] = (tag_[0] & 0x80) | size; 
	}
	int set_kv(Session* session, const char* key, int key_size, const char* val, int val_size) {
		int payload_size = key_size + val_size;
		int max_payload_size = get_payload_size();
		
		if (!IsFree()) {
			return -1;
		}
		if (max_payload_size < payload_size) {
			return -1;
		}
		memcpy((void*) payload_, key, key_size);
		memcpy((void*) &payload_[key_size], val, val_size);
		tag_[0] = 0x80 | payload_size; // mark as allocated and set size in a single op
		ScmFence();
		return 0;
	}
	char* get_key() { 
		return (char*) payload_; 
	}
	int get_keysize() { 
		return get_payload_size() - VAL_SIZE; 
	}
	char* get_val() { 
		return (char*) &payload_[get_keysize()];
	}
	int get_valsize() {
		return VAL_SIZE;
	}
	char get_tag() {
		return tag_[0];
	}

	// Assumes callee checked boundary is within bucket_page
	inline Entry* NextEntry() {
		return Entry::Load(&tag_[get_size()]);
	}
private:
	volatile char tag_[TAG_SIZE];
	volatile char payload_[];
};


template<typename Session>
class Page {
public:
	Page()
	{
		Init();
	}

	int Init() {
		int             payload_size = PAGE_SIZE - sizeof(next_) - TAG_SIZE;
		
		next_ = 0x0;
		if (Entry<Session>::Make(&b_[0], true, payload_size) == NULL) {
			return -1;
		}
		return 0;
	}	

	static Page* Make(Session* session)
	{
		void* ptr;
		if (session->salloc()->AllocateExtent(session, sizeof(Page), 0, &ptr) < 0) {
			dbg_log(DBG_ERROR, "No storage available");
		}
		return new(ptr) Page();
	}


	Entry<Session>* GetEntry(int pos) {
		return Entry<Session>::Load(&b_[pos]);
	}
	
	bool IsEmpty() {
		Entry<Session>* entry = Entry<Session>::Load(&b_[0]);
		return (entry->get_size() == PAGE_SIZE - sizeof(next_) 
		        ? true : false);
	}

	Page* Next() {
		return (Page*) next_;
	}

	void set_next(Page* next) {
		next_ = (uint64_t) next;
	}

	int Insert(Session* session, const char* key, int key_size, uint64_t val);
	int Insert(Session* session, const char* key, int key_size, const char* val, int val_size);
	int Search(Session* session, const char* key, int key_size, uint64_t* val);
	int Search(Session* session, const char* key, int key_size, char** val, int* val_size);
	int Delete(Session* session, const char* key, int key_size);
	int SplitHalf(Session* session, Page* splitover_page);
	int Split(Session* session, Page* splitover_page, const SplitPredicate& split_predicate);
	int Merge(Session* session, Page* other_page);
	void Print();
	struct dentry {
                char key[128];
                uint64_t val;
                struct dentry *next_dentry;
        };
        int return_dentry(void *);


private:
	union {
		struct {
			volatile uint64_t u64_[PAGE_SIZE/sizeof(uint64_t) - 1];
			volatile uint64_t next_;
		};
		volatile char b_[PAGE_SIZE];
	};

	int Delete(Session* session, Entry<Session>* entry, Entry<Session>* prev_entry, Entry<Session>* next_entry);
};


//////////////////////////////////////////////////////////////////////////////
//
//  Page
//
//////////////////////////////////////////////////////////////////////////////



// If no space available then -E_NOMEM is returned 
template<typename Session>
int 
Page<Session>::Insert(Session* session, const char* key, int key_size, 
                      const char* val, int val_size)
{
	Entry<Session>*  entry;
	Entry<Session>*  free_entry=NULL;
	int              payload_size = key_size + val_size;
	int              i;
	int              step;
	uint64_t         uval;

	for (i=0; i < PAGE_SIZE - (int) sizeof(next_); i+=step)
	{
		entry = GetEntry(i);
		step = entry->get_size();
		if (entry->IsFree()) {
			if (entry->get_payload_size() >= payload_size) {
				free_entry = GetEntry(i);
				break;
			}
		}
	}

	if (free_entry) {
		// Split the free entry into allocated entry and a new smaller 
		// free entry
		int max_payload_size = free_entry->get_payload_size();
		if (max_payload_size - payload_size - TAG_SIZE >= 0) {
			Entry<Session>::Split(session, free_entry, TAG_SIZE+payload_size);
		}

		uval = *((uint64_t*) val);
		free_entry->set_kv(session, key, key_size, val, val_size);
		return 0;
	}

	return -E_NOMEM;
}


// If no space available then -E_NOMEM is returned 
template<typename Session>
int 
Page<Session>::Insert(Session* session, const char* key, int key_size, uint64_t val)
{
	return Insert(session, key, key_size, (char*) &val, sizeof(val));
}


template<typename Session>
int 
Page<Session>::Search(Session* session, const char *key, int key_size, char** valp, 
                      int* val_sizep)
{
	unsigned int    i;
	int             step;
	Entry<Session>* entry;
	int             val_size;

	if (IsEmpty() == true) {
		return -1;
	}

	for (i=0; i < PAGE_SIZE - sizeof(next_); i+=step)
	{
		entry = GetEntry(i);
		step = entry->get_size();
		if (!entry->IsFree() && 
		    (entry->get_keysize() == key_size)) 
		{
			if (memcmp(entry->get_key(), key, key_size) == 0) {
				val_size = entry->get_valsize();
				if (valp) {
					memcpy(*valp, (char*) &b_[i+TAG_SIZE+key_size], val_size);
				}	
				if (val_sizep) {
					*val_sizep = val_size;
				}	
				return 0;
			}
		}	
	}

	return -1;
}


template<typename Session>
int 
Page<Session>::Search(Session* session, const char *key, int key_size, uint64_t* val)
{
	int val_size;
	return Search(session, key, key_size, (char**) &val, &val_size);
}


template<typename Session>
int 
Page<Session>::Delete(Session* session, Entry<Session>* entry, Entry<Session>* prev_entry, Entry<Session>* next_entry)
{
	int size;

	entry->set_free(session);
	size = entry->get_size();
	size += (next_entry && next_entry->IsFree() == true 
			 ? next_entry->get_size(): 0);

	// Coalesce contiguous empty slots
	if (prev_entry && prev_entry->IsFree() == true) {
		size += prev_entry->get_size(); 
		prev_entry->set_size(session, size); 
	} else {
		entry->set_size(session, size);
	}
	return 0;
}


template<typename Session>
int 
Page<Session>::Delete(Session* session, const char *key, int key_size)
{
	unsigned int    i;
	int             step;
	Entry<Session>* entry;
	Entry<Session>* prev_entry = NULL;
	Entry<Session>* next_entry;

	for (i=0; i < PAGE_SIZE - sizeof(next_); i+=step)
	{
		entry = GetEntry(i);
		step = entry->get_size();
		if (!entry->IsFree() && 
		    (entry->get_payload_size() == key_size + VAL_SIZE)) 
		{	
			if (memcmp((char*) &b_[i+1], key, key_size) == 0) {
				// Coalesce contiguous empty slots
				if (i+step < PAGE_SIZE - sizeof(next_)) {
					next_entry = entry->NextEntry();
				} else {
					next_entry = NULL;
				}
				return Delete(session, entry, prev_entry, next_entry);
			}
		}
		prev_entry = entry;
	}
	return -1;
}


// Move the second half of the entries of the current bucket page into 
// the provided bucket page
//
// Callee is responsible for allocating the new bucket page
//
// If provided bucket page overflows, then remaining entries stay in the 
// current bucket page and error -E_NOMEM is returned 
template<typename Session>
int 
Page<Session>::SplitHalf(Session* session, Page* splitover_page)
{
	int              ret;
	int              i;
	int              size;
	Entry<Session>*  entry;
	Entry<Session>*  prev_entry = NULL;
	Entry<Session>*  next_entry;
	int              count;
	int              split_count;
	uint64_t         val;

	// count the number of entries
	for (i=0, count=0; i < PAGE_SIZE - sizeof(next_); i+=size)
	{
		entry = GetEntry(i);
		size = entry->get_size();
		if (!entry->IsFree())
		{
			count++;
		}	
	}
	if (count < 2) {
		// there are not enough entries to split
		return -1;
	}
	split_count=count/2;

	// split the bucket page in half
	for (i=0, count=0; i < PAGE_SIZE - sizeof(next_); i+=size)
	{
		entry = GetEntry(i);
		size = entry->get_size();
		if (!entry->IsFree())
		{
			count++;
			if (i+size < PAGE_SIZE - sizeof(next_)) {
				next_entry = entry->NextEntry();
			} else {
				next_entry = NULL;
			}
			if (count > split_count) {
				//FIXME: when journaling, we need to be careful about ordering and overlap
				//of inserts and deletes as reads are not bypassed through buffered 
				//writes. 
				val = *((uint64_t*) entry->get_val());
				if ((ret = splitover_page->Insert(session, entry->get_key(), 
				                                  entry->get_keysize(), val))!=0) 
				{
					return ret;
				}
				// It's safe to delete entries while iterating as tags
				// of follow up entries are untouched
				assert(Delete(session, entry, prev_entry, next_entry) == 0);
			}
		}	
		prev_entry = entry;
	}

	return 0;
}


// Split entries of the current bucket page between current page 
// and provided bucket page using provided split function
//
// Callee is responsible for allocating the new bucket page
//
// If provided bucket page overflows, then remaining entries stay in the 
// current bucket page and error -E_NOMEM is returned 
template<typename Session>
int 
Page<Session>::Split(Session* session, Page* splitover_page, const SplitPredicate& split_predicate)
{
	int             ret;
	unsigned int    i;
	unsigned int    size;
	Entry<Session>* entry;
	Entry<Session>* prev_entry = NULL;
	Entry<Session>* next_entry;
	uint64_t        val;
	char*           key;
	int             keysize;

	for (i=0; i < PAGE_SIZE - sizeof(next_); i+=size)
	{
		entry = GetEntry(i);
		size = entry->get_size();
		if (!entry->IsFree())
		{
			if (i+size < PAGE_SIZE - sizeof(next_)) {
				next_entry = entry->NextEntry();
			} else {
				next_entry = NULL;
			}
			key = entry->get_key();
			keysize = entry->get_keysize();
			if (split_predicate(key, keysize)) {
				//FIXME: when journaling, we need to be careful about ordering and overlap
				//of inserts and deletes as reads are not bypassed through buffered 
				//writes. 
				val = *((uint64_t*) entry->get_val());
				if ((ret = splitover_page->Insert(session, key, keysize, val)) != 0) {
					return ret;
				}
				// It's safe to delete entries while iterating as tags
				// of follow up entries are untouched
				assert(Delete(session, entry, prev_entry, next_entry) == 0);
			}
		}	
		prev_entry = entry;
	}

	return 0;
}


// Move the entries of the provided bucket page into the current bucket page.
//
// If current bucket page overflows, then remaining entries stay in the 
// provided bucket page and error -E_NOMEM is returned 
template<typename Session>
int 
Page<Session>::Merge(Session* session, Page* other_page)
{
	int             ret;
	int             i;
	int             size;
	Entry<Session>* entry;
	Entry<Session>* prev_entry = NULL;
	Entry<Session>* next_entry;
	int             count;
	uint64_t        val;

	for (i=0, count=0; i < PAGE_SIZE - sizeof(next_); i+=size)
	{
		entry = other_page->GetEntry(i);
		size = entry->get_size();
		if (!entry->IsFree())
		{
			if (i+size < PAGE_SIZE - sizeof(next_)) {
				next_entry = entry->NextEntry();
			} else {
				next_entry = NULL;
			}
			//FIXME: when journaling, we need to be careful about ordering and overlap
			//of inserts and deletes as reads are not bypassed through buffered 
			//writes. 
			val = *((uint64_t*) entry->get_val());
			if ((ret = Insert(session, entry->get_key(), 
			                  entry->get_keysize(), val)) != 0) 
			{
				return ret;
			}
			// It's safe to delete entries while iterating as tags
			// of follow up entries are untouched
			assert(other_page->Delete(session, entry, prev_entry, next_entry) == 0);
		}	
		prev_entry = entry;
	}

	return 0;
}


template<typename Session>
void 
Page<Session>::Print()
{
	int             i;
	uint64_t        val;
	int             size;
	Entry<Session>* entry;

	for (i=0; i < PAGE_SIZE - sizeof(next_); i+=size)
	{
		entry = GetEntry(i);
		size = entry->get_size();
		val = *((uint64_t*) entry->get_val());
		if (!entry->IsFree()) {
			printf("(%s, %llu) ", entry->get_key(), val);
		}
	}
}

template<typename Session>
int
Page<Session>::return_dentry(void *head_addr)
{

        int             i;
        uint64_t        val;
        int             size;
        struct dentry   *dentry_list_item;
        struct dentry   *curr_ptr;
        curr_ptr = (struct dentry *)((struct dentry *)(head_addr))->val;
     
        struct dentry *a = NULL, *b;
        Entry<Session>* entry;

        for (i=0; i < PAGE_SIZE - sizeof(next_); i+=size)
        {    
                entry = GetEntry(i);
                size = entry->get_size();
                val = *((uint64_t*) entry->get_val());
                if (!entry->IsFree()) {

                        dentry_list_item = new struct dentry;
                        strcpy(dentry_list_item->key, entry->get_key());
                        dentry_list_item->val = val;
                        dentry_list_item->next_dentry = NULL;

                        curr_ptr->next_dentry = dentry_list_item;
                        curr_ptr = curr_ptr->next_dentry;

                }
        }
        ((struct dentry *)(head_addr))->val = (uint64_t)curr_ptr;
}


//////////////////////////////////////////////////////////////////////////////
//
//  Bucket
//
//////////////////////////////////////////////////////////////////////////////


// Class Bucket encapsulates the primary bucket page
template<typename Session>
class Bucket {
public:
	Bucket()
	{
		Init();
	}

	int Init() {
		return 0;
	}	
	
	int Insert(Session* session, const char* key, int key_size, uint64_t val);
	int Insert(Session* session, const char* key, int key_size, const char* val, int val_size);
	int Search(Session* session, const char* key, int key_size, uint64_t* val);
	int Search(Session* session, const char* key, int key_size, char** val, int* val_size);
	int Delete(Session* session, const char* key, int key_size);
	int Split(Session* session, Bucket* new_bucket, const SplitPredicate& split_predicate);
	int Merge(Session* session, Bucket*);
	void Print();
        int return_dentry(void *);


private:
	Page<Session> page_;

	int Delete(Session* session, Entry<Session>* entry, Entry<Session>* prev_entry, Entry<Session>* next_entry);
};


template<typename Session>
int 
Bucket<Session>::Insert(Session* session, const char* key, int key_size, 
                        const char* val, int val_size)
{
	int            ret;
	Page<Session>* page;
	Page<Session>* last_page=0x0;

	// try inserting into the primary page first and otherwise
	// into an overflow page if such a page exists
	for (page=&page_; page != 0x0; page=page->Next()) {
		// FIXME: should we trigger re-hash when inserting in an 
		// existing overflow page???
		if ((ret=page->Insert(session, key, key_size, val, val_size)) == 0) {
			return 0;
		}
		last_page = page;
	}
	assert(last_page != 0x0);

	// No page had space. Insert the KV pair in a new page.
	
	page = Page<Session>::Make(session);
	page->Insert(session, key, key_size, val, val_size);
	last_page->set_next(page);

	//TODO: trigger re-hash 

	return HT_REHASH;
}


template<typename Session>
int 
Bucket<Session>::Insert(Session* session, const char* key, int key_size, uint64_t val)
{
	return Insert(session, key, key_size, (char*) &val, sizeof(val));
}


template<typename Session>
int 
Bucket<Session>::Search(Session* session, const char *key, int key_size, char** valp, 
                        int* val_sizep)
{
	PROFILER_PREAMBLE
	int            ret;
	Page<Session>* page;

	for (page=&page_; page != 0x0; page=page->Next()) {
		if ((ret=page->Search(session, key, key_size, valp, val_sizep)) == 0) {
			return ret;
		}
	}

	return -1;
}


template<typename Session>
int 
Bucket<Session>::Search(Session* session, const char* key, int key_size, uint64_t* val)
{
	int val_size;
	return Search(session, key, key_size, (char**) &val, &val_size);
}


template<typename Session>
int 
Bucket<Session>::Delete(Session* session, const char* key, int key_size)
{
	int            ret;
	Page<Session>* page;

	for (page=&page_; page != 0x0; page=page->Next()) {
		if ((ret=page->Delete(session, key, key_size)) == 0) {
			return ret;
		}
	}

	return -1;
}


template<typename Session>
int 
Bucket<Session>::Split(Session* session, Bucket* splitover_bucket, 
                       const SplitPredicate& split_predicate)
{
	int            ret;
	Page<Session>* page;
	Page<Session>* new_page;
	Page<Session>* splitover_page;

	for (page=&page_; page != 0x0; page=page->Next()) {
		splitover_page = &splitover_bucket->page_;
dosplit:
		ret=page->Split(session, splitover_page, split_predicate);
		if (ret==-E_NOMEM) {
			if ((new_page=splitover_page->Next()) == 0x0) {
				//FIXME: protect against cycle-loop resulting from infinite splits. 
				//       is this possible? shouldn't splits converge?
				new_page = Page<Session>::Make(session);
				splitover_page->set_next(new_page);
			}
			splitover_page = new_page;
			goto dosplit;
		}
	}
	return 0;
}


template<typename Session>
void Bucket<Session>::Print()
{
	Page<Session>* page;

	for (page=&page_; page != 0x0; page=page->Next()) {
		printf("Page: %p = {\n", page);
		page->Print();
		printf("}\n");
	}
}

template<typename Session>
int
Bucket<Session>::return_dentry(void *head_addr)
{

  Page<Session>* page;

        for (page=&page_; page != 0x0; page=page->Next()) {
                page->return_dentry(head_addr);
        }    

}



//////////////////////////////////////////////////////////////////////////////
//
//  HashTable
//
//////////////////////////////////////////////////////////////////////////////


template<typename Session>
class HashTable {
public:
	HashTable()
		: 
		  ncount_(0),
		  size_log2_(5),
		  split_idx_(0)
	{ }

	static HashTable* Make(Session* session)
	{
		void* ptr;
		if (session->salloc()->AllocateExtent(session, sizeof(HashTable), 0, &ptr) < 0) {
			dbg_log(DBG_ERROR, "No storage available");
		}
		return new(ptr) HashTable();
	}
	
	static HashTable* Make(Session* session, void* buf)
	{
		HashTable* ht;
		void*      ptr;

		return new(buf) HashTable();
	}

	int Init();

	int Insert(Session* session, const char* key, int key_size, const char* val, int val_size);
	int Insert(Session* session, const char* key, int key_size, uint64_t val);
	int Search(Session* session, const char *key, int key_size, char** valp, int* val_sizep);
	int Search(Session* session, const char* key, int key_size, uint64_t* val);
	int Delete(Session* session, const char* key, int key_size);
	void Print();
	int Size(Session* session) { return ncount_; }
	 int return_dentry(void *);


private:
	class LinearSplit;

	inline uint32_t Index(Session* session, const char* key, int key_size);
	static uint32_t ModHash(uint32_t fh, int size_log2_, int shift) 
	{
		uint32_t mask = (1 << (size_log2_ + shift)) - 1;
		return fh & mask;
	}
	
	uint32_t         ncount_;               // number of entries 
	uint32_t         size_log2_;            // log2 of size 
	uint32_t         split_idx_;
	Bucket<Session>  buckets_[NUM_BUCKETS];
};


template<typename Session>
class HashTable<Session>::LinearSplit: public SplitPredicate {
public:
	LinearSplit(uint32_t size_log2)
		: size_log2_(size_log2)
	{ }

	bool operator() (const char* key, int key_size) const 
	{
		uint32_t             curidx;
		uint32_t             newidx;
		uint32_t             fh;

		fh = hashlittle(key, key_size, 0);
		curidx = ModHash(fh, size_log2_, 0);
		newidx = ModHash(fh, size_log2_, 1);
		return (newidx != curidx) ? true: false;
	}

private:
	uint32_t size_log2_;
};


template<typename Session>
int
HashTable<Session>::Init()
{
	split_idx_ = 0;
	size_log2_ = 5;
	ncount_ = 0;
	return E_SUCCESS;
}


template<typename Session>
uint32_t 
HashTable<Session>::Index(Session* session, const char* key, int key_size)
{
	uint32_t idx;
	uint32_t fh;

	fh = hashlittle(key, key_size, 0);
	if ((idx = ModHash(fh, size_log2_, 0)) < split_idx_ ) {
		idx = ModHash(fh, size_log2_, 1);
	}
	return idx;
}


template<typename Session>
int 
HashTable<Session>::Insert(Session* session, const char* key, int key_size, 
                           const char* val, int val_size)
{
	uint32_t         idx;
	Bucket<Session>* bucket;
	Bucket<Session>* splitover_bucket;
	int              ret;

	idx = Index(session, key, key_size);
	bucket = &buckets_[idx];
	ncount_++;
	if ((ret=bucket->Insert(session, key, key_size, val, val_size))==0) {
		return 0;
	}
	if (ret == HT_REHASH) {
		bucket = &buckets_[split_idx_];
		splitover_bucket = &buckets_[split_idx_+(1<<size_log2_)];
		bucket->Split(session, splitover_bucket, LinearSplit(size_log2_));
		split_idx_++;
		if (split_idx_ == (1U << size_log2_)) {
			split_idx_ = 0;
			size_log2_++;
			if ((1U << size_log2_) > NUM_BUCKETS) {
				assert(0); // tried to rehash and resize but not enough buckets
			}
		}
	}
	return 0;
}


template<typename Session>
int 
HashTable<Session>::Insert(Session* session, const char* key, int key_size, uint64_t val)
{
	return Insert(session, key, key_size, (char*) &val, sizeof(val));
}


template<typename Session>
int 
HashTable<Session>::Search(Session* session, const char *key, int key_size, 
                           char** valp, int* val_sizep)
{
	PROFILER_PREAMBLE
	uint32_t         idx;
	Bucket<Session>* bucket;

	PROFILER_SAMPLE
	idx = Index(session, key, key_size);
	PROFILER_SAMPLE
	bucket = &buckets_[idx];
	//return bucket->Search(session, key, key_size, valp, val_sizep);
	int ret = bucket->Search(session, key, key_size, valp, val_sizep);
	PROFILER_SAMPLE
	return ret;
}


template<typename Session>
int 
HashTable<Session>::Search(Session* session, const char* key, int key_size, 
                           uint64_t* val)
{
	int val_size;

	return Search(session, key, key_size, (char**) &val, &val_size);
}


template<typename Session>
int 
HashTable<Session>::Delete(Session* session, const char* key, int key_size)
{
	// TODO: trigger merge when number of entries drops below a threshold
	//       check the literature of linear hashing of when this happens

	uint32_t idx = Index(session, key, key_size);
	Bucket<Session>* bucket = &buckets_[idx];
	ncount_--;
	return bucket->Delete(session, key, key_size);
}


template<typename Session>
void 
HashTable<Session>::Print()
{
	int              i;
	uint32_t         idx;
	Bucket<Session>* bucket;

	for (i=0; i<(1<<size_log2_)+split_idx_; i++) {
		bucket = &buckets_[i];
		bucket->Print();
	}	
}

template<typename Session>
int
HashTable<Session>::return_dentry(void *head_addr)
{
        int              i;
        uint32_t         idx;
        Bucket<Session>* bucket;

        for (i=0; i<(1<<size_log2_)+split_idx_; i++) {
                bucket = &buckets_[i];
                bucket->return_dentry(head_addr);
        }





}


#undef volatile 

#endif // __STAMNOS_OSD_COMMON_HASHTABLE_H 
